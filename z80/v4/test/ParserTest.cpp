#include <catch2/catch_test_macros.hpp>

#ifdef SPECBOLT_MODULES
import z80_v4;
#else
#include "refract/Parser.hpp"
#endif

namespace specbolt::v4 {

using namespace refract;

TEST_CASE("Parser tests") {
  Parser parser(R"(# I am a comment
I am the second line
I am the third line
#MAGIC!)");
  SECTION("Starts out sensibly") {
    CHECK(!parser.eof());
    CHECK(parser.line() == 1);
  }
  SECTION("Takes up to a delimiter and tracks line numbers") {
    CHECK(parser.take_until('\n') == "# I am a comment");
    CHECK(parser.line() == 2);
    CHECK(parser.take_until('\n') == "I am the second line");
    CHECK(parser.line() == 3);
    CHECK(parser.take_until('\n') == "I am the third line");
    CHECK(parser.line() == 4);
    // No delimiter left, so this takes the rest.
    CHECK(parser.take_until('\n') == "#MAGIC!");
    CHECK(parser.line() == 4);
    CHECK(parser.eof());
  }
  SECTION("A line says which one it was") {
    const auto [number, text] = parser.next_line();
    CHECK(number == 1);
    CHECK(text == "# I am a comment");
    CHECK(parser.next_line().number == 2);
  }
  SECTION("Counts the lines it passes over in one go") {
    CHECK(parser.take_until('\n') == "# I am a comment");
    CHECK(parser.take_until('#') == "I am the second line\nI am the third line\n");
    CHECK(parser.rest() == "MAGIC!");
    CHECK(parser.line() == 4);
  }
  SECTION("A field is trimmed, which is what a row's three columns want") {
    Parser row("  00000000 n  |  ld a, $nn  | ld8 a <- n  ");
    CHECK(row.next_field('|') == "00000000 n");
    CHECK(row.next_field('|') == "ld a, $nn");
    // The last column has no `|` after it, so it is whatever is left.
    CHECK(Parser::trim(row.rest()) == "ld8 a <- n");
  }
  SECTION("Words skip the blanks between them") {
    Parser words("  vocab   reg = a b");
    words.skip_word();
    CHECK(words.next_word() == "reg");
    CHECK(words.next_word() == "=");
    CHECK(words.next_word() == "a");
  }
  SECTION("Skips and accounts for lines") {
    Parser spaces("  \n\n  hello");
    spaces.skip_any(" \n");
    CHECK(spaces.rest() == "hello");
    CHECK(spaces.line() == 3);
  }
  SECTION("Skips to the end") {
    Parser blanks(" \n ");
    blanks.skip_any(" \n");
    CHECK(blanks.eof());
    CHECK(blanks.line() == 2);
  }
  SECTION("Skips nothing") {
    Parser none("hello");
    none.skip_any(" \n");
    CHECK(none.rest() == "hello");
    CHECK(none.line() == 1);
  }
}
} // namespace specbolt::v4
