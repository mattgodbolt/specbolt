#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Table.hpp"

// The parser reads whatever description it is handed, so these drive it with
// their own tables rather than damaging the real one to see what it says. Every
// message a malformed table can produce should have a case here.

namespace specbolt::v4 {
namespace {

// Generous fixed capacities: the descriptions below are a few lines each.
constexpr std::size_t max_fields = 4;
constexpr std::size_t max_tables = 3;
constexpr std::size_t max_rows = 8;

struct Parsed {
  std::array<Field, max_fields> fields{};
  std::array<std::string_view, max_tables> tables{};
  std::array<Row, max_rows> rows{};
};

// Runs the whole pipeline, including the checks that are `static_assert`s
// against the real description.
Parsed parse(const std::string_view description) {
  Parsed parsed;
  parsed.fields = parse_fields<max_fields>(description);
  parsed.tables = parse_tables<max_tables>(description);
  parsed.rows = parse_rows<max_rows>(description, parsed.fields, parsed.tables);
  const auto row_count = count_matching(description, &is_row);
  const std::span rows{parsed.rows.data(), row_count};
  for (const auto &row: rows)
    check_immediates(row);
  check_row_precedence(rows, parsed.fields, parsed.tables.size());
  return parsed;
}

using Catch::Matchers::Equals;

} // namespace

TEST_CASE("Table diagnostics") {
  SECTION("Rows must live in a table") {
    CHECK_THROWS_WITH(parse("00000000 | nop | nop\n"),
        Equals("z80.cpu:1: this row is not in any table; declare one with `table <name>` first"));
  }
  SECTION("Opcode patterns") {
    CHECK_THROWS_WITH(parse("table t\n0101 | nop | nop\n"), Equals("z80.cpu:2: opcode pattern must be 8 characters"));
    CHECK_THROWS_WITH(parse("table t\n00pp0p01 | nop | nop\n"),
        Equals("z80.cpu:2: opcode pattern has non-contiguous bits for a field"));
    CHECK_THROWS_WITH(
        parse("table t\nabcde001 | nop | nop\n"), Equals("z80.cpu:2: opcode pattern has too many fields"));
  }
  SECTION("The three columns must agree about immediates") {
    CHECK_THROWS_WITH(parse("table t\n00000000 n | ld a, $nnnn | ld8 a <- n\n"),
        Equals("z80.cpu:2: the mnemonic renders a different number of immediate bytes than the encoding fetches"));
    CHECK_THROWS_WITH(parse("table t\n00000000 n | ld a, $nn | ld8 a <- a\n"),
        Equals("z80.cpu:2: the action and the encoding disagree about whether there is an immediate"));
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | ld8 a <- nn\n"),
        Equals("z80.cpu:2: write 'n'; the encoding column says how many bytes it occupies"));
    CHECK_THROWS_WITH(
        parse("table t\n00000000 d | nop | nop\n"), Equals("z80.cpu:2: 'd' is not an encoding byte; expected 'n'"));
  }
  SECTION("Row precedence") {
    CHECK_THROWS_WITH(parse("table t\n00000000 | nop | nop\n00000000 | also nop | nop\n"),
        Equals("z80.cpu:3: an earlier row shadows this one completely"));
    CHECK_THROWS_WITH(parse("field r = b c d e h l m a\ntable t\n001101xx | frob | nop\n00yyy100 | inc {r:y} | nop\n"),
        Equals("z80.cpu:3: this row overlaps a later one without being contained by it"));
    CHECK_THROWS_WITH(parse("field w = - - - -\ntable t\n101wwzzz | {w} | nop\n"),
        Equals("z80.cpu:3: this row matches no opcode at all"));
  }
  SECTION("Vocabularies") {
    CHECK_THROWS_WITH(parse("field rr = a b\ntable t\n"), Equals("z80.cpu:1: field name must be a single character"));
    CHECK_THROWS_WITH(parse("field r\ntable t\n"), Equals("z80.cpu:1: expected '=' in field declaration"));
    CHECK_THROWS_WITH(parse("field r =\ntable t\n"), Equals("z80.cpu:1: field declares no values"));
    CHECK_THROWS_WITH(parse("field r = a b\nfield r = c d\ntable t\n"), Equals("z80.cpu:2: duplicate field name"));
    CHECK_THROWS_WITH(parse("field r = a b c d e f g h i\ntable t\n"), Equals("z80.cpu:1: too many values in field"));
    CHECK_THROWS_WITH(parse("field r = a/wat=1 b\ntable t\n"),
        Equals("z80.cpu:1: 'wat' is not a member attribute; expected 'delay'"));
    CHECK_THROWS_WITH(parse("field r = a/delay=xx b\ntable t\n"), Equals("z80.cpu:1: delay must be a single digit"));
    CHECK_THROWS_WITH(parse("field r = a:add8+n b\ntable t\n"),
        Equals("z80.cpu:1: a vocabulary member cannot append an immediate; only the encoding fetches those"));
  }
  SECTION("References") {
    CHECK_THROWS_WITH(parse("table t\n00yyy000 | inc {q:y} | nop\n"),
        Equals("z80.cpu:2: reference names a vocabulary that does not exist"));
    CHECK_THROWS_WITH(parse("field r = a b\ntable t\n00000000 | inc {r:y} | nop\n"),
        Equals("z80.cpu:3: reference names a field the opcode pattern does not define"));
    CHECK_THROWS_WITH(parse("field r = a b\ntable t\n00yyy000 | inc {r:y} | nop\n"),
        Equals("z80.cpu:3: vocabulary has the wrong number of values for its opcode bits"));
    CHECK_THROWS_WITH(parse("field r = a b\ntable t\n00000000 | inc {r | nop\n"),
        Equals("z80.cpu:3: unterminated field reference in mnemonic"));
  }
  SECTION("Tables") {
    CHECK_THROWS_WITH(parse("table\n"), Equals("z80.cpu:1: table declaration has no name"));
    CHECK_THROWS_WITH(parse("table t u\n"), Equals("z80.cpu:1: table declaration takes a single name"));
    CHECK_THROWS_WITH(parse("table t\ntable t\n"), Equals("z80.cpu:2: duplicate table name"));
    CHECK_THROWS_WITH(
        parse("table t\n00000000 | nop | goto elsewhere\n"), Equals("z80.cpu:2: no table named 'elsewhere'"));
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
  SECTION("A well-formed table raises nothing") {
    CHECK_NOTHROW(parse("field r = b c\ntable t\n0000000y | ld {r:y} | ld8 {r:y} <- a\n"));
  }
}

} // namespace specbolt::v4
