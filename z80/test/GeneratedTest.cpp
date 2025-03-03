#include <iostream>

#include "peripherals/Memory.hpp"
#include "z80/Generated.hpp"
#include "z80/Z80.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <ranges>

namespace specbolt {

TEST_CASE("Opcode generation tests") {
  SECTION("Test base opcode disassembly") {
    const auto &names = base_opcode_names();
    CHECK(names[0x00] == "nop");
    CHECK(names[0x01] == "ld bc, $nnnn");
    CHECK(names[0x11] == "ld de, $nnnn");
    CHECK(names[0x21] == "ld hl, $nnnn");
    CHECK(names[0x31] == "ld sp, $nnnn");
    CHECK(names[0x10] == "djnz $d");
  }
}

} // namespace specbolt
