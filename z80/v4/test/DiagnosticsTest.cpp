#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Table.hpp"

// The parser reads whatever description it is handed, so these drive it with
// their own tables rather than damaging the real one to see what it says. Every
// message the *parse* can produce should have a case here.
//
// The generator's messages cannot: `find_location`, `find_operation` and
// `operand_for_parameter` are `consteval`, so a description they reject is a
// compile error rather than something a test can catch. Their messages are
// covered by the description compiling at all, and by reading them.

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
Parsed parse(const std::string_view text) {
  Parsed parsed;
  check_every_line_means_something(text);
  parsed.vocabularies = parse_vocabularies(text);
  parsed.tables = parse_tables(text, parsed.vocabularies);
  parsed.rows = parse_rows(text, parsed.vocabularies, parsed.tables);
  const auto opcodes = opcodes_of_each(parsed.vocabularies, parsed.rows);
  parsed.decoded = decode_tables(parsed.rows, opcodes, parsed.tables);
  static_cast<void>(latched_tables(parsed.rows, parsed.tables.size()));

  const Description description{parsed.vocabularies, parsed.rows, parsed.tables, parsed.decoded, entry_table};
  check_row_precedence(description, opcodes);
  check_derived_rows_override(description, opcodes);
  check_tables_used(description);
  check_inherited_literals(description);
  check_displacement_rendered(description);
  return parsed;
}

// Totality is the one check a description here has to opt into: a table is only
// total once it answers for all 256 opcodes, and a case making some other point
// would have to say so in full before it could say anything else.
void check_total(const std::string_view text) {
  const auto parsed = parse(text);
  check_tables_total({parsed.vocabularies, parsed.rows, parsed.tables, parsed.decoded, entry_table});
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
    CHECK_THROWS_WITH(parse("vocab r = a:add8(n) b\ntable t\n"),
        Equals("z80.cpu:1: a member cannot pass an immediate; only the encoding fetches those"));
    CHECK_THROWS_WITH(
        parse("vocab r = a:add8(0 b\ntable t\n"), Equals("z80.cpu:1: a member's argument list is not closed"));
    CHECK_THROWS_WITH(parse("vocab r = a:add8() b\ntable t\n"),
        Equals("z80.cpu:1: a member's argument list is empty; leave it off rather than writing '()'"));
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
    CHECK(member_of({.vocabularies = parsed.vocabularies, .matched = row.matched, .opcode = 0x00}, //
              reference)
              .display == "b");
    CHECK(
        member_of({.vocabularies = parsed.vocabularies, .matched = row.matched, .rules = derived.rules, .opcode = 0x00},
            reference)
            .display == "ixh");
    CHECK(
        member_of({.vocabularies = parsed.vocabularies, .matched = row.matched, .rules = derived.rules, .opcode = 0x01},
            reference)
            .display == "c");
    CHECK(resolve({.vocabularies = parsed.vocabularies, .matched = row.matched, .rules = derived.rules, .opcode = 0x00},
              row.steps[0].destinations[0])
              .name == Name{"ixh"});

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
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | ld8 a <- ((hl))\n"),
        Equals("z80.cpu:2: an address cannot itself be indirect"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | ld8 a <- hl+d\n"),
        Equals("z80.cpu:2: a displacement only makes sense inside '(...)'"));
    CHECK_THROWS_WITH(parse("table t\n00000000 n | nop | ld8 a <- nn\n"),
        Equals("z80.cpu:2: write 'n'; the encoding column says how many bytes it occupies"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | ld8 a <- (hl)/wobble=1\n"),
        Equals("z80.cpu:2: 'wobble=1' is not an operand attribute"));
    CHECK_THROWS_WITH(
        parse("table t\n00000000 | nop | ld8 a <- (hl)/delay=12\n"), Equals("z80.cpu:2: delay must be a single digit"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | \n"), Equals("z80.cpu:2: row has no action"));
  }
  SECTION("Steps") {
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | if\n"), Equals("z80.cpu:2: 'if' needs something to test"));
    CHECK_THROWS_WITH(
        parse("table t\n00000000 | nop | goto t u\n"), Equals("z80.cpu:2: goto takes a single table name"));
    CHECK_THROWS_WITH(parse("vocab i = ix iy\ntable t\n11011101 | (dd) | goto u(ix\ntable u(view:i)\n"
                            "00000000 | frob | nop\n"),
        Equals("z80.cpu:3: unterminated '(' in goto"));
  }
  SECTION("Mnemonics") {
    CHECK_THROWS_WITH(parse("table t\n00000000 n | ld a, $x | ld8 a <- n\n"),
        Equals("z80.cpu:2: expected $nn, $nnnn or $e in mnemonic"));
    CHECK_THROWS_WITH(parse("table t\n00000000 n n | ld ($nnnn), $nnnn | ld8 (n) <- n\n"),
        Equals("z80.cpu:2: a row renders at most one immediate; the encoding only fetches one"));
  }
  SECTION("Vocabulary members and their scopes") {
    CHECK_THROWS_WITH(parse("vocab r = b :add8\ntable t\n"), Equals("z80.cpu:1: a vocabulary member has no name"));
    // The scope is the next word, so this only fires when the `:` ends the line;
    // `vocab r : = b c` takes `=` for the scope and complains about the missing one.
    CHECK_THROWS_WITH(parse("vocab r :\ntable t\n"),
        Equals("z80.cpu:1: ':' introduces the scope a vocabulary's members come from, and none was given"));
    CHECK_THROWS_WITH(parse("vocab r : = b c\ntable t\n"), Equals("z80.cpu:1: expected '=' in vocabulary declaration"));
    CHECK_THROWS_WITH(parse("vocab r = b:add8(0,1,2,3)\ntable t\n"),
        Equals("z80.cpu:1: a member passes more arguments than an operation can take"));
    CHECK_THROWS_WITH(parse("vocab r = b:add8(n)\ntable t\n"),
        Equals("z80.cpu:1: a member cannot pass an immediate; only the encoding fetches those"));
  }
  SECTION("Derived tables, further") {
    constexpr std::string_view base = "vocab r = b c\ntable t\n11011101 | (u) | goto u\n0000000y | ld {r:y} | nop\n";
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = t with q.b -> c\n"),
        Equals("z80.cpu:5: substitution names a vocabulary that does not exist"));
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = t with r.b - c\n"),
        Equals("z80.cpu:5: expected '->' in table substitution 'r.b - c'"));
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = t with rb -> c\n"),
        Equals("z80.cpu:5: a table substitution names the vocabulary it rewrites, as in "
               "'vocabulary.member -> replacement'"));
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = t with r.b -> {r:y}\n"),
        Equals("z80.cpu:5: only a table that takes a view may substitute a view reference"));
    CHECK_THROWS_WITH(parse(std::string(base) + "table u = t with\n"),
        Equals("z80.cpu:5: a derived table declares no substitutions, so it is its parent"));
    // A substitution's right side is parsed against an empty pattern, so a
    // reference that is not the table's view fails for want of a slice first.
    CHECK_THROWS_WITH(parse("vocab r = b c\nvocab i = ix iy\ntable t\n11011101 | (dd) | goto u(ix)\n"
                            "table u(view:i) = t with r.b -> {r:y}\n0000000y | ld {r:y} | nop\n"),
        Equals("z80.cpu:5: reference names a slice the opcode pattern does not define"));
  }
  SECTION("Immediates and displacements are counted, not guessed") {
    CHECK_THROWS_WITH(parse("table t\n00000000 n n n | ld a, $nnnn | ld8 a <- n\n"),
        Equals("z80.cpu:2: an instruction may carry at most two immediate bytes"));
    CHECK_THROWS_WITH(parse("vocab m = (ix+d) (iy+d)\ntable t\n0000000y | ld {m:y} | ld8 {m:y} <- (hl+d)\n"),
        Equals("z80.cpu:3: an instruction may only be displaced through one base"));
    CHECK_THROWS_WITH(parse("vocab r = b $nn\ntable t\n"),
        Equals("z80.cpu:1: a vocabulary member cannot render an immediate; only the encoding fetches those"));
    CHECK_THROWS_WITH(parse("vocab r = b (a+d)+d\ntable t\n"), Equals("z80.cpu:1: member text is too complicated"));
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
  SECTION("Every opcode of every table must decode to something") {
    CHECK_THROWS_WITH(check_total("table t\n00000000 | nop | nop\n"),
        Equals("z80.cpu:1: table 't' does not say what opcode 1 does; add a row, or `xxxxxxxx` last to catch the "
               "rest"));
    CHECK_NOTHROW(check_total("table t\n00000000 | nop | nop\nxxxxxxxx | ?? | nop\n"));
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
  SECTION("A table's view is declared with itself and a vocabulary") {
    CHECK_THROWS_WITH(parse("vocab i = ix iy\ntable t\n11011101 | (dd) | goto u(ix)\ntable u(view)\n"),
        Equals("z80.cpu:4: a table view names itself and a vocabulary, as in 'indexed(view:index)'"));
    CHECK_THROWS_WITH(parse("vocab i = ix iy\ntable t\n11011101 | (dd) | goto u(ix)\ntable u(view:i\n"),
        Equals("z80.cpu:4: unterminated '(' in table view"));
    CHECK_THROWS_WITH(parse("vocab i = ix iy\ntable t\n11011101 | (dd) | goto u(ix)\ntable u(view:nope)\n"),
        Equals("z80.cpu:4: table view names a vocabulary that does not exist"));
  }
  SECTION("A view reference must line up with the view it is selected by") {
    // `r` has three members and the view has two, so no opcode could pick
    // between them: the reference has nothing to mean.
    CHECK_THROWS_WITH(parse("vocab i = ix iy\nvocab r = b c d\ntable t\n11011101 | (dd) | goto u(ix)\n"
                            "table u(view:i)\n00000000 | ld {r:view} | ld8 {r:view} <- a\n"),
        Equals("z80.cpu:6: vocabulary has a different number of members than the table's view"));
    CHECK_THROWS_WITH(parse("vocab i = ix iy\nvocab r = b c\ntable t\n11011101 | (dd) | goto u\n"
                            "0000000y | ld {r:y} | ld8 {r:y} <- a\ntable u = t with r.b -> {i:view}\n"),
        Equals("z80.cpu:6: only a table that takes a view may substitute a view reference"));
  }
  SECTION("A goto says which view the table it enters is decoded under") {
    constexpr std::string_view vocabs = "vocab i = ix iy\nvocab j = bc de\n";
    CHECK_THROWS_WITH(parse(std::string(vocabs) + "table t\n11011101 | (dd) | goto u\ntable u(view:i)\n"
                                                  "00000000 | frob {i:view} | ld16 {i:view} <- {i:view}\n"),
        Equals("z80.cpu:4: this table takes a view, so the goto must say which"));
    CHECK_THROWS_WITH(parse(std::string(vocabs) + "table t\n11011101 | (dd) | goto u(ix)\ntable u\n"
                                                  "00000000 | frob | nop\n"),
        Equals("z80.cpu:4: this table takes no view, so the goto may not supply one"));
    CHECK_THROWS_WITH(parse(std::string(vocabs) + "table t\n11011101 | (dd) | goto u(nope)\ntable u(view:i)\n"
                                                  "00000000 | frob {i:view} | ld16 {i:view} <- {i:view}\n"),
        Equals("z80.cpu:4: goto names a view that is not a member of that table's view vocabulary"));
    // Handing a view on rather than choosing one: the two tables have to agree
    // about what the value means, which is the vocabulary it is drawn from.
    CHECK_THROWS_WITH(parse(std::string(vocabs) + "table t(view:i)\n11001011 | (cb) | goto u(view)\n"
                                                  "00000000 | frob {i:view} | ld16 {i:view} <- {i:view}\n"
                                                  "table u(view:j)\n"
                                                  "00000000 | frob {j:view} | ld16 {j:view} <- {j:view}\n"),
        Equals("z80.cpu:4: the view being handed on is drawn from a different vocabulary"));
  }
  SECTION("Every member of a vocabulary a view selects must have the same shape") {
    // Nothing that runs at compile time can know which member a view will pick,
    // so every check resolves at member 0 and applies the answer to all of them.
    // Members that disagree would make that silently wrong rather than wrong
    // out loud: one addressing mode executed and another printed.
    constexpr std::string_view prefix = "vocab i = ix iy\ntable t\n11011101 | (dd) | goto u(ix)\n"
                                        "table u(view:i)\n00000000 | ld {m:view} | ld8 {m:view} <- a\n";
    CHECK_NOTHROW(parse("vocab m = (ix+d)/delay=1 (iy+d)/delay=1\n" + std::string(prefix)));
    // The mistake this exists for: one member displaced and the other not, so
    // the `fd` page would run `(iy+d)` and print `(iy)`.
    // Line 1 is the declaration, which is what has to change; line 6 is the row
    // that selects it by a view, without which the declaration would be fine.
    CHECK_THROWS_WITH(parse("vocab m = (ix+d)/delay=1 (iy)\n" + std::string(prefix)),
        Equals("z80.cpu:1: vocabulary 'm' is selected by a view (at z80.cpu:6), so all of its members must have the "
               "same shape as '(ix+d)'; '(iy)' does not"));
    // Disagreeing about the idle cycle a write-back costs is just as silent.
    CHECK_THROWS_WITH(parse("vocab m = (ix+d)/delay=1 (iy+d)\n" + std::string(prefix)),
        Equals("z80.cpu:1: vocabulary 'm' is selected by a view (at z80.cpu:6), so all of its members must have the "
               "same shape as '(ix+d)'; '(iy+d)' does not"));
    // A hole cannot be one of them either: a view has no opcode bits to leave
    // room for a more specific row in.
    CHECK_THROWS_WITH(parse("vocab m = ix -\n" + std::string(prefix)),
        Equals("z80.cpu:1: vocabulary 'm' is selected by a view (at z80.cpu:6), so all of its members must have the "
               "same shape as 'ix'; '-' does not"));
    // And a member may not bring an operation at all. The operation is spliced
    // from member 0, so two that disagree would run the first one's for every
    // view: the `fd` page would execute `ld16` where the row said `inc16`.
    CHECK_THROWS_WITH(parse("vocab m = ix:ld16 iy:inc16\n" + std::string(prefix)),
        Equals("z80.cpu:1: vocabulary 'm' is selected by a view (at z80.cpu:6), so each of its members may only name "
               "a location, since a view is chosen long after the operation has been spliced; 'ix' does not"));
    // Agreeing about the operation is not enough either: the arguments a member
    // fixes are spliced from member 0 in the same way.
    CHECK_THROWS_WITH(parse("vocab m = ix:add8(0) iy:add8(carry)\n" + std::string(prefix)),
        Equals("z80.cpu:1: vocabulary 'm' is selected by a view (at z80.cpu:6), so each of its members may only name "
               "a location, since a view is chosen long after the operation has been spliced; 'ix' does not"));
  }
  SECTION("A view named like a slice letter is an ambiguity, not a silent win") {
    // The view is matched by name before a slice is looked for, so `{r:y}` in a
    // table whose view is `y` would resolve to the view: the opcode's `y` bits
    // would be read by nothing, the row would still claim both opcodes, and both
    // would decode to whichever member the prefix chose.
    constexpr std::string_view prefix = "vocab r = b c\nvocab i = ix iy\ntable t\n11011101 | (dd) | goto u(ix)\n";
    CHECK_THROWS_WITH(parse(std::string(prefix) + "table u(y:i)\n0000000y | ld {r:y} | ld8 {r:y} <- a\n"),
        Equals("z80.cpu:6: 'y' is this table's view and also a slice of this opcode, so this reference could mean "
               "either; rename one of them"));
    // Naming the view something no pattern uses is what the Z80's own table does.
    CHECK_NOTHROW(parse(std::string(prefix) + "table u(view:i)\n0000000y | ld {r:y} | ld8 {r:y} <- a\n"));
    // A view may still share its name with a slice the *pattern does not define*:
    // nothing is ambiguous there, and the reference means the view.
    CHECK_NOTHROW(parse(std::string(prefix) + "table u(y:i)\n00000000 | ld {r:y} | ld8 {r:y} <- a\n"));
  }
  SECTION("A well-formed table raises nothing") {
    CHECK_NOTHROW(parse("vocab r = b c\ntable t\n0000000y | ld {r:y} | ld8 {r:y} <- a\n"));
  }
}

} // namespace specbolt::v4
