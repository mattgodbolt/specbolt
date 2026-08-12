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
  SECTION("Parses lines and tracks line numbers") {
    const auto first = parser.split_to('\n');
    CHECK(first.data() == "# I am a comment");
    CHECK(first.line() == 1);
    CHECK(parser.line() == 2);
    CHECK(parser.split_to('\n').data() == "I am the second line");
    CHECK(parser.line() == 3);
    CHECK(parser.split_to('\n').data() == "I am the third line");
    CHECK(parser.line() == 4);
    CHECK(parser.split_to('\n').data() == "#MAGIC!");
    CHECK(parser.line() == 4);
    CHECK(parser.eof());
  }
  SECTION("Tracks even when skipping") {
    parser.split_to('\n'); // skip first line

    const auto lines = parser.split_to('#'); // skip to MAGIC
    CHECK(lines.data() == "I am the second line\nI am the third line\n");
    CHECK(lines.line() == 2);
    CHECK(parser.data() == "MAGIC!");
    CHECK(parser.line() == 4);
  }
  SECTION("Skips and accounts for lines") {
    Parser spaces("  \n\n  hello");
    spaces.skip_any(" \n");
    CHECK(spaces.data() == "hello");
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
    CHECK(none.data() == "hello");
    CHECK(none.line() == 1);
  }
}
} // namespace specbolt::v4
