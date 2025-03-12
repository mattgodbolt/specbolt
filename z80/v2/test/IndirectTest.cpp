#include <iostream>

#ifdef SPECBOLT_MODULES
import z80v2;
#else
#include "z80/v2/Z80Impl.hpp"
#endif


#include <bitset>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <format>

namespace specbolt::v2 {

struct build_is_indirect {
  template<auto op>
  static constexpr bool result = op.indirect;
};

TEST_CASE("Check indirect flags") {
  static constexpr std::bitset<256> expected_but_backwards{
      // clang-format off
    // 0000000000000000                0000000000000000
    //                 0000000000000000                0000000000000000
      "0000000000000000000000000000000000000000000000000000111000000000"
      "0000001000000010000000100000001000000010000000101111110100000010"
      "0000001000000010000000100000001000000010000000100000001000000010"
      "0000000000000000000000000000000000000000000000000000000000000000"
      // clang-format-on
    };
  for (auto opcode = 0uz; opcode < 256uz; ++opcode) {
    INFO(std::format("opcode: 0x{:02x}", opcode));
    CHECK(impl::table<build_is_indirect>[opcode] == expected_but_backwards[255-opcode]);
  }
}

}
