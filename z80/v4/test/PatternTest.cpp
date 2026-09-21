#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

#include "refract/Pattern.hpp"

namespace specbolt::v4 {

using namespace refract;

TEST_CASE("Opcode bit parsing") {
  SECTION("LD xx, IMM16") {
    constexpr auto matched = parse_pattern("00pp0001");
    STATIC_CHECK(matched.opcode_bits == 0b00000001);
    STATIC_CHECK(matched.slices.size() == 1);
    STATIC_CHECK(matched.slices[0] == BitSlice{.name = 'p', .shift = 4, .mask = 3});
  }
  SECTION("LD r, r'") {
    constexpr auto matched = parse_pattern("01yyyzzz");
    STATIC_CHECK(matched.opcode_bits == 0b01000000);
    STATIC_CHECK(matched.slices.size() == 2);
    STATIC_CHECK(matched.slices[0] == BitSlice{.name = 'y', .shift = 3, .mask = 7});
    STATIC_CHECK(matched.slices[1] == BitSlice{.name = 'z', .shift = 0, .mask = 7});
  }
  SECTION("Wholly fixed") {
    constexpr auto matched = parse_pattern("11001001");
    STATIC_CHECK(matched.opcode_bits == 0xc9);
    STATIC_CHECK(matched.slices.size() == 0);
  }
  SECTION("Wholly variable") {
    constexpr auto matched = parse_pattern("nnnnnnnn");
    STATIC_CHECK(matched.opcode_bits == 0);
    STATIC_CHECK(matched.slices[0] == BitSlice{.name = 'n', .shift = 0, .mask = 0xff});
  }
  SECTION("Rejects bad patterns") {
    CHECK_THROWS(parse_pattern("0101"));
    CHECK_THROWS(parse_pattern("011011011"));
    CHECK_THROWS(parse_pattern("00pp0p01"));
    CHECK_THROWS(parse_pattern("abcde001"));
  }
  SECTION("Accepts the widest supported field count") {
    constexpr auto matched = parse_pattern("wwxxyyzz");
    STATIC_CHECK(matched.slices.size() == Pattern::max_slices);
  }
}

constexpr auto ld_rr_imm16 = parse_pattern("00pp0001");

// A pattern is only ever read in one direction: `place` builds the opcodes a row claims, and `extract` reads a value
// back out of one. Nothing tests an opcode *against* a pattern, because nothing does that. See `opcodes_of`.
TEST_CASE("Opcode slices") {
  constexpr auto matched = ld_rr_imm16;
  SECTION("Extracts and places field values") {
    STATIC_CHECK(matched.slices[0].extract(0x21) == 2);
    STATIC_CHECK(matched.slices[0].place(3) == 0x30);
    STATIC_CHECK(std::ranges::all_of(std::views::iota(0, ld_rr_imm16.slices[0].mask + 1), [](const int value) {
      constexpr auto slice = ld_rr_imm16.slices[0];
      const auto narrowed = static_cast<std::uint8_t>(value);
      return slice.extract(static_cast<std::uint8_t>(ld_rr_imm16.opcode_bits | slice.place(narrowed))) == narrowed;
    }));
  }
}

} // namespace specbolt::v4
