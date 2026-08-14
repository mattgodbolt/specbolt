#include <catch2/catch_test_macros.hpp>

#ifdef SPECBOLT_MODULES
import z80_v4;
#else
#include "refract/Parser.hpp"
#endif

#include <ranges>
#include <vector>

namespace specbolt::v4 {

using namespace refract;

TEST_CASE("Parser tests") {
  Parser parser("# I am a comment");
  SECTION("Starts out sensibly") { CHECK(!parser.eof()); }
  SECTION("Takes up to a delimiter") {
    Parser lines("one\ntwo\nthree");
    CHECK(lines.take_until('\n') == "one");
    CHECK(lines.take_until('\n') == "two");
    // No delimiter left, so this takes the rest.
    CHECK(lines.take_until('\n') == "three");
    CHECK(lines.eof());
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
  SECTION("Trimming takes blanks from both ends") {
    CHECK(Parser::trim("  hello  ") == "hello");
    CHECK(Parser::trim("\thello\t") == "hello");
    // A description written on a machine that ends its lines with \r\n.
    CHECK(Parser::trim("hello\r") == "hello");
    CHECK(Parser::trim("   ").empty());
    CHECK(Parser::trim("").empty());
  }
  SECTION("Skips to the first character not in the set") {
    Parser spaces("  \n\n  hello");
    spaces.skip_any(" \n");
    CHECK(spaces.rest() == "hello");
  }
  SECTION("Skips to the end") {
    Parser blanks(" \n ");
    blanks.skip_any(" \n");
    CHECK(blanks.eof());
  }
  SECTION("Skips nothing") {
    Parser none("hello");
    none.skip_any(" \n");
    CHECK(none.rest() == "hello");
  }
}

TEST_CASE("Lines are numbered from one") {
  const auto numbers_and_text = [](const std::string_view description) {
    return lines_of(description) |
           std::views::transform([](const Line &line) { return std::pair{line.number, line.text}; }) |
           std::ranges::to<std::vector>();
  };
  using Lines = std::vector<std::pair<std::size_t, std::string_view>>;
  SECTION("Every line carries the number a diagnostic names it by") {
    CHECK(numbers_and_text("one\ntwo\nthree") == Lines{{1, "one"}, {2, "two"}, {3, "three"}});
  }
  SECTION("Each line arrives trimmed, because every caller wants it that way") {
    CHECK(numbers_and_text("  one  \n\ttwo\r") == Lines{{1, "one"}, {2, "two"}});
  }
  SECTION("A blank line still counts, or everything after it would be misnamed") {
    CHECK(numbers_and_text("one\n\nthree") == Lines{{1, "one"}, {2, ""}, {3, "three"}});
  }
  SECTION("A trailing newline ends a line rather than starting one worth reading") {
    CHECK(numbers_and_text("one\n") == Lines{{1, "one"}, {2, ""}});
  }
  SECTION("An empty description has no lines at all") { CHECK(numbers_and_text("").empty()); }
}
} // namespace specbolt::v4
