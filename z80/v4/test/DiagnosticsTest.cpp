#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Table.hpp"

// The parser reads whatever description it is handed, so these drive it with
// their own tables rather than damaging the real one to see what it says. Every
// message a malformed table can produce should have a case here.

namespace specbolt::v4 {

using namespace refract;
namespace {

struct Parsed {
  std::vector<Vocabulary> vocabularies;
  std::vector<TableDecl> tables;
  std::vector<Row> rows;
  std::vector<DecodeTable> decoded;
};

// Runs the whole pipeline, including the checks that are `static_assert`s
// against the real description. Nothing here is `constexpr`: the same functions
// serve a test that wants a message and a build that wants a diagnostic.
Parsed parse(const std::string_view description) {
  Parsed parsed;
  check_every_line_means_something(description);
  parsed.vocabularies = parse_vocabularies(description);
  parsed.tables = parse_tables(description, parsed.vocabularies);
  parsed.rows = parse_rows(description, parsed.vocabularies, parsed.tables);
  const auto opcodes = opcodes_of_each(parsed.vocabularies, parsed.rows);
  check_row_precedence(parsed.rows, opcodes, parsed.tables.size());
  check_derived_rows_override(parsed.rows, opcodes, parsed.tables);
  static_cast<void>(latched_tables(parsed.rows, parsed.tables.size()));
  check_tables_used(parsed.rows, parsed.tables, entry_table);
  parsed.decoded = decode_tables(parsed.rows, opcodes, parsed.tables);
  check_inherited_literals(parsed.rows, parsed.tables, parsed.decoded);
  return parsed;
}

using Catch::Matchers::Equals;

} // namespace

TEST_CASE("Table diagnostics") {
  SECTION("A line that is not a comment, a declaration or a row is a mistake") {
    CHECK_THROWS_WITH(parse("table t\n00000000 nop nop\n"),
        Equals("z80.cpu:2: this is not a comment, a declaration, or a row; a row needs its '|' separators"));
    CHECK_THROWS_WITH(parse("vocabularies r = a b\ntable t\n"),
        Equals("z80.cpu:1: this is not a comment, a declaration, or a row; a row needs its '|' separators"));
  }
  SECTION("Rows must live in a table") {
    CHECK_THROWS_WITH(parse("00000000 | nop | nop\n"),
        Equals("z80.cpu:1: this row is not in any table; declare one with `table <name>` first"));
  }
  SECTION("Opcode patterns") {
    CHECK_THROWS_WITH(parse("table t\n0101 | nop | nop\n"), Equals("z80.cpu:2: opcode pattern must be 8 characters"));
    CHECK_THROWS_WITH(parse("table t\n00pp0p01 | nop | nop\n"),
        Equals("z80.cpu:2: opcode pattern has non-contiguous bits for a slice"));
    CHECK_THROWS_WITH(
        parse("table t\nabcde001 | nop | nop\n"), Equals("z80.cpu:2: opcode pattern has too many slices"));
  }
  SECTION("The three columns must agree about immediates") {
    CHECK_THROWS_WITH(parse("table t\n00000000 n | ld a, $nnnn | ld8 a <- n\n"),
        Equals("z80.cpu:2: the mnemonic renders a different number of immediate bytes than the encoding fetches"));
    CHECK_THROWS_WITH(parse("table t\n00000000 n | ld a, $nn | ld8 a <- a\n"),
        Equals("z80.cpu:2: the action and the encoding disagree about whether there is an immediate"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | ld8 a <- nn\n"),
        Equals("z80.cpu:2: write 'n'; the encoding column says how many bytes it occupies"));
    CHECK_THROWS_WITH(parse("table t\n00000000 x | nop | nop\n"),
        Equals("z80.cpu:2: 'x' is not an encoding byte; expected 'n' or 'd'"));
    CHECK_THROWS_WITH(
        parse("table t\n00000000 d d | nop | nop\n"), Equals("z80.cpu:2: a row reads at most one displacement"));
  }
  SECTION("Row precedence") {
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | nop\n00000000 | also nop | nop\n"),
        Equals("z80.cpu:3: an earlier row shadows this one completely"));
    CHECK_THROWS_WITH(parse("vocab r = b c d e h l m a\ntable t\n001101xx | frob | nop\n00yyy100 | inc {r:y} | nop\n"),
        Equals("z80.cpu:3: this row overlaps a later one without being contained by it"));
    CHECK_THROWS_WITH(parse("vocab w = - - - -\ntable t\n101wwzzz | {w:w} | nop\n"),
        Equals("z80.cpu:3: this row matches no opcode at all"));
  }
  SECTION("Vocabularies") {
    CHECK_THROWS_WITH(parse("vocab\ntable t\n"), Equals("z80.cpu:1: vocabulary declaration has no name"));
    CHECK_THROWS_WITH(parse("vocab r\ntable t\n"), Equals("z80.cpu:1: expected '=' in vocabulary declaration"));
    CHECK_THROWS_WITH(parse("vocab r =\ntable t\n"), Equals("z80.cpu:1: vocabulary declares no members"));
    CHECK_THROWS_WITH(parse("vocab r = a b\nvocab r = c d\ntable t\n"), Equals("z80.cpu:2: duplicate vocabulary name"));
    CHECK_THROWS_WITH(
        parse("vocab r = a b c d e f g h i\ntable t\n"), Equals("z80.cpu:1: too many members in vocabulary"));
    CHECK_THROWS_WITH(parse("vocab r = a/wat=1 b\ntable t\n"),
        Equals("z80.cpu:1: 'wat' is not a member attribute; expected 'delay'"));
    CHECK_THROWS_WITH(parse("vocab r = a/delay=xx b\ntable t\n"), Equals("z80.cpu:1: delay must be a single digit"));
    CHECK_THROWS_WITH(parse("vocab r = a:add8+n b\ntable t\n"),
        Equals("z80.cpu:1: a vocabulary member cannot append an immediate; only the encoding fetches those"));
  }
  SECTION("References") {
    CHECK_THROWS_WITH(parse("table t\n00yyy000 | inc {q:y} | nop\n"),
        Equals("z80.cpu:2: reference names a vocabulary that does not exist"));
    CHECK_THROWS_WITH(parse("vocab r = a b\ntable t\n00000000 | inc {r:y} | nop\n"),
        Equals("z80.cpu:3: reference names a slice the opcode pattern does not define"));
    CHECK_THROWS_WITH(parse("vocab r = a b\ntable t\n00yyy000 | inc {r:y} | nop\n"),
        Equals("z80.cpu:3: vocabulary has the wrong number of members for its opcode bits"));
    CHECK_THROWS_WITH(parse("vocab r = a b\ntable t\n00000000 | inc {r | nop\n"),
        Equals("z80.cpu:3: unterminated vocabulary reference in mnemonic"));
  }
  SECTION("Tables") {
    CHECK_THROWS_WITH(parse("table\n"), Equals("z80.cpu:1: table declaration has no name"));
    CHECK_THROWS_WITH(
        parse("table t u\n"), Equals("z80.cpu:1: expected '= <parent> with <substitutions>' after the table name"));
    CHECK_THROWS_WITH(parse("table t\ntable t\n"), Equals("z80.cpu:2: duplicate table name"));
    CHECK_THROWS_WITH(
        parse("table t\n00000000 | nop | goto elsewhere\n"), Equals("z80.cpu:2: no table named 'elsewhere'"));
  }
  SECTION("Derived tables") {
    constexpr std::string_view base = "vocab r = b c\ntable t\n11011101 | (u) | goto u\n0000000y | ld {r:y} | nop\n";
    CHECK_NOTHROW(parse(std::string(base) + "table u = t with r.b -> c\n"));
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = nowhere with r.b -> c\n"),
        Equals("z80.cpu:5: no table named 'nowhere' is declared above this one"));
    // A parent must come first, so that a chain resolves in declaration order.
    CHECK_THROWS_WITH(parse("table u = t with r.b -> c\ntable t\n00000000 | nop | nop\n"),
        Equals("z80.cpu:1: no table named 't' is declared above this one"));
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = u with r.b -> c\n"),
        Equals("z80.cpu:5: no table named 'u' is declared above this one"));
    CHECK_THROWS_WITH(
        parse(std::string(base) + "table u = t\n"), Equals("z80.cpu:5: expected 'with' after the parent table name"));
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = t with r.b\n"),
        Equals("z80.cpu:5: expected '->' in table substitution 'r.b'"));
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = t with r.b ->\n"),
        Equals("z80.cpu:5: a table substitution needs a name on each side of '->'"));
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = t with r.b -> -\n"),
        Equals("z80.cpu:5: a substitution cannot rename something to nothing; a hole belongs in a vocabulary"));
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = t with r.b -> n\n"),
        Equals("z80.cpu:5: a vocabulary member must name something the CPU can resolve"));
  }
  SECTION("A view's own row must fit inside the row it displaces") {
    // The row in `u` claims both opcodes; the parent keeps one for itself, and
    // swallowing it would take that instruction off the prefixed page entirely.
    constexpr std::string_view shared = "vocab r = b c\ntable t\n11011101 | (dd) | goto u\n";
    CHECK_NOTHROW(parse(std::string(shared) + "0000000y | ld {r:y} | ld8 {r:y} <- a\n"
                                              "table u = t with r.b -> ixh\n0000000y | frob | nop\n"));
    CHECK_THROWS_WITH(parse(std::string(shared) + "00000000 | special | nop\n0000000y | ld {r:y} | nop\n"
                                                  "table u = t with r.b -> ixh\n0000000y | frob | nop\n"),
        Equals("z80.cpu:7: this row overlaps one it inherits from 't' without replacing it or fitting inside it, "
               "so it takes opcodes that row meant to keep"));
  }
  SECTION("A view must not silently inherit a row that spells the renamed name out") {
    constexpr std::string_view shared = "vocab p = bc hl\ntable t\n11011101 | (dd) | goto u\n0000000y | ld {p:y} | ";
    // Naming the vocabulary is fine: a rule reaches it.
    CHECK_NOTHROW(parse(std::string(shared) + "ld16 {p:y} <- {p:y}\ntable u = t with p.hl -> ix\n"));
    // A rule whose left side is parenthesised has to match the same way.
    CHECK_THROWS_WITH(parse("vocab p = bc (hl)\ntable t\n11011101 | (dd) | goto u\n0000000y | ld {p:y} | "
                            "ld16 {p:y} <- (hl)\ntable u = t with p.(hl) -> (ix+d)\n"),
        Equals("z80.cpu:4: table 'u' renames '(hl)', and this row names it literally where a rule cannot reach it; "
               "give that table its own row, or name a vocabulary"));
    // Spelling it out is not, because a rule never rewrites literal text.
    CHECK_THROWS_WITH(parse(std::string(shared) + "ld16 {p:y} <- hl\ntable u = t with p.hl -> ix\n"),
        Equals("z80.cpu:4: table 'u' renames 'hl', and this row names it literally where a rule cannot reach it; "
               "give that table its own row, or name a vocabulary"));
  }
  SECTION("A derived table decodes its parent's rows, renamed") {
    const auto parsed = parse("vocab r = b c\ntable t\n11011101 | (u) | goto u\n0000000y | ld {r:y} | ld8 {r:y} <- a\n"
                              "table u = t with r.b -> ixh\n00000000 | frob | nop\n");
    const auto &derived = parsed.tables[1];
    const auto &row = parsed.rows[1];
    const auto reference = row.pieces[1].reference;

    // The substitution reaches whatever the row names, and only that member.
    CHECK(member_of(parsed.vocabularies, reference, row.matched, 0x00).display == "b");
    CHECK(member_of(parsed.vocabularies, reference, row.matched, 0x00, derived.rules).display == "ixh");
    CHECK(member_of(parsed.vocabularies, reference, row.matched, 0x01, derived.rules).display == "c");
    CHECK(resolve(parsed.vocabularies, row.steps[0].destinations[0], row.matched, 0x00, derived.rules).name ==
          Name{"ixh"});

    // Opcode 0 is the derived table's own row; 1 it inherits; 0xdd it inherits,
    // which is what makes `dd dd` re-enter.
    CHECK(parsed.decoded[1][0x00].value() == 2);
    CHECK(parsed.decoded[1][0x01].value() == 1);
    CHECK(parsed.decoded[1][0xdd].value() == 0);
    CHECK_FALSE(parsed.decoded[1][0x02].has_value());
  }
  SECTION("Operands") {
    CHECK_THROWS_WITH(
        parse("table t\n00000000 | nop | ld8 a <- (hl\n"), Equals("z80.cpu:2: unterminated '(' in operand '(hl'"));
    CHECK_THROWS_WITH(
        parse("table t\n00000000 | nop | ld8 a <- 0xzz\n"), Equals("z80.cpu:2: malformed constant '0xzz'"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | ld8 a <- 70000\n"),
        Equals("z80.cpu:2: constant '70000' does not fit in 16 bits"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | ld8 a <- averyverylongname\n"),
        Equals("z80.cpu:2: operand name 'averyverylongname' is too long"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | ld8 a <- -\n"),
        Equals("z80.cpu:2: '-' discards a result, so it can only be a destination"));
  }
  SECTION("Immediates count wherever they appear") {
    // An immediate destination is how `ld (nn), a` is written, and it used to be
    // rejected because only operands were counted.
    CHECK_NOTHROW(parse("table t\n00110010 n n | ld ($nnnn), a | ld8 (n) <- a\n"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | ld (hl), a | ld8 (n) <- a\n"),
        Equals("z80.cpu:2: the action and the encoding disagree about whether there is an immediate"));
  }
  SECTION("A vocabulary member must name something resolvable") {
    CHECK_THROWS_WITH(parse("vocab s = bc de hl n\ntable t\n"),
        Equals("z80.cpu:1: a vocabulary member must name something the CPU can resolve"));
  }
  SECTION("Tables must be reachable and non-empty") {
    CHECK_THROWS_WITH(
        parse("table t\n00000000 | nop | nop\ntable dead\n"), Equals("z80.cpu:3: this table has no rows"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | nop\ntable dead\n00000000 | frob | nop\n"),
        Equals("z80.cpu:3: no goto reaches this table, so nothing in it is ever checked"));
  }
  SECTION("A latched table must be reached the same way every time") {
    CHECK_NOTHROW(parse("table t\n11001011 d | (cb) | goto u\n00000000 | nop | nop\n"
                        "table u\n00000000 | frob | nop\n"));
    CHECK_THROWS_WITH(parse("table t\n11001011 d | (cb) | goto u\n11011101 | (dd) | goto u\n"
                            "table u\n00000000 | frob | nop\n"),
        Equals("z80.cpu:3: this table is reached both with and without a displacement"));
  }
  SECTION("Gotos may form a cycle") {
    // Decoding is a loop, and every turn of it fetches a byte, so a table that
    // reaches itself makes progress rather than recursing. `dd dd dd ...` needs
    // exactly this.
    CHECK_NOTHROW(parse("table t\n11011101 | (t) | goto t\n00000000 | nop | nop\n"));
    CHECK_NOTHROW(parse("table t\n11001011 | (u) | goto u\ntable u\n00000000 | back | goto t\n"));
  }
  SECTION("Every fixed capacity says so when it is reached") {
    // DD/FD will push on several of these, so what happens at the edge matters:
    // each is a `Vector` whose overflow names the table's limit, not the C++ one.
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | nop ; nop ; nop ; nop ; nop ; nop ; nop\n"),
        Equals("z80.cpu:2: row has too many steps"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | ld8 a <- a a a a a\n"), Equals("z80.cpu:2: too many operands"));
    CHECK_THROWS_WITH(
        parse("table t\n00000000 | nop | ld8 a a a a a <- a\n"), Equals("z80.cpu:2: too many destinations"));
    CHECK_THROWS_WITH(parse("vocab r = b c\ntable t\n0000000y | {r:y}x{r:y}x{r:y}x{r:y}x{r:y}x{r:y}x{r:y}x | nop\n"),
        Equals("z80.cpu:3: mnemonic is too complicated"));
    CHECK_THROWS_WITH(parse("vocab r = b c\ntable t\n11011101 | (u) | goto u\n0000000y | ld {r:y} | nop\n"
                            "table u = t with r.b->c, r.c->b, r.b->c, r.c->b, r.b->c, r.c->b, r.b->c\n"),
        Equals("z80.cpu:5: too many substitutions in table"));
  }
  SECTION("A well-formed table raises nothing") {
    CHECK_NOTHROW(parse("vocab r = b c\ntable t\n0000000y | ld {r:y} | ld8 {r:y} <- a\n"));
  }
}

} // namespace specbolt::v4
