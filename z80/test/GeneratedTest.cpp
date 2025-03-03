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
    CHECK(names[0x02] == "ld (bc), a");
    CHECK(names[0x03] == "inc bc");
    CHECK(names[0x04] == "inc b");
    CHECK(names[0x05] == "dec b");
    CHECK(names[0x08] == "ex af, af'");
    CHECK(names[0x0a] == "ld a, (bc)");
    CHECK(names[0x0b] == "dec bc");
    CHECK(names[0x0c] == "inc c");
    CHECK(names[0x0d] == "dec c");
    CHECK(names[0x10] == "djnz $d");
    CHECK(names[0x11] == "ld de, $nnnn");
    CHECK(names[0x12] == "ld (de), a");
    CHECK(names[0x13] == "inc de");
    CHECK(names[0x14] == "inc d");
    CHECK(names[0x15] == "dec d");
    CHECK(names[0x18] == "jr $d");
    CHECK(names[0x1a] == "ld a, (de)");
    CHECK(names[0x1b] == "dec de");
    CHECK(names[0x1c] == "inc e");
    CHECK(names[0x1d] == "dec e");
    CHECK(names[0x20] == "jr nz $d");
    CHECK(names[0x21] == "ld hl, $nnnn");
    CHECK(names[0x22] == "ld ($nnnn), hl");
    CHECK(names[0x23] == "inc hl");
    CHECK(names[0x24] == "inc h");
    CHECK(names[0x25] == "dec h");
    CHECK(names[0x28] == "jr z $d");
    CHECK(names[0x2a] == "ld hl, ($nnnn)");
    CHECK(names[0x2b] == "dec hl");
    CHECK(names[0x2c] == "inc l");
    CHECK(names[0x2d] == "dec l");
    CHECK(names[0x30] == "jr nc $d");
    CHECK(names[0x31] == "ld sp, $nnnn");
    CHECK(names[0x32] == "ld ($nnnn), a");
    CHECK(names[0x33] == "inc sp");
    CHECK(names[0x34] == "inc (hl)");
    CHECK(names[0x35] == "dec (hl)");
    CHECK(names[0x38] == "jr c $d");
    CHECK(names[0x3a] == "ld a, ($nnnn)");
    CHECK(names[0x3b] == "dec sp");
    CHECK(names[0x3c] == "inc a");
    CHECK(names[0x3d] == "dec a");
  }
}

} // namespace specbolt
