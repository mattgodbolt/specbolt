#include <iostream>

#include "peripherals/Memory.hpp"
#include "z80/Generated.hpp"
#include "z80/Z80.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <ranges>

namespace specbolt {

TEST_CASE("Opcode generation tests") {
  constexpr auto base_address = 0x8000;
  auto dis = [](auto... bytes) {
    Memory memory;
    for (auto &&[index, val]: std::views::enumerate(std::array{bytes...})) {
      memory.write(static_cast<std::uint16_t>(base_address + index), static_cast<std::uint8_t>(val));
    }
    const auto [disassembly, length] = disassemble(memory, base_address);
    CHECK(length == sizeof...(bytes));
    return disassembly;
  };
  SECTION("Test base opcode disassembly") {
    CHECK(dis(0x00) == "nop");
    CHECK(dis(0x01, 0x34, 0x12) == "ld bc, 0x1234");
    CHECK(dis(0x02) == "ld (bc), a");
    CHECK(dis(0x03) == "inc bc");
    CHECK(dis(0x04) == "inc b");
    CHECK(dis(0x05) == "dec b");
    CHECK(dis(0x06, 0xce) == "ld b, 0xce");
    CHECK(dis(0x07) == "rlca");
    CHECK(dis(0x08) == "ex af, af'");
    CHECK(dis(0x0a) == "ld a, (bc)");
    CHECK(dis(0x0b) == "dec bc");
    CHECK(dis(0x0c) == "inc c");
    CHECK(dis(0x0d) == "dec c");
    CHECK(dis(0x0e, 0x0f) == "ld c, 0x0f");
    CHECK(dis(0x0f) == "rrca");
    CHECK(dis(0x10, 0xfe) == "djnz 0x8000");
    CHECK(dis(0x11, 0x02, 0x01) == "ld de, 0x0102");
    CHECK(dis(0x12) == "ld (de), a");
    CHECK(dis(0x13) == "inc de");
    CHECK(dis(0x14) == "inc d");
    CHECK(dis(0x15) == "dec d");
    CHECK(dis(0x16, 0x00) == "ld d, 0x00");
    CHECK(dis(0x17) == "rla");
    CHECK(dis(0x18, 0x40) == "jr 0x8042");
    CHECK(dis(0x1a) == "ld a, (de)");
    CHECK(dis(0x1b) == "dec de");
    CHECK(dis(0x1c) == "inc e");
    CHECK(dis(0x1d) == "dec e");
    CHECK(dis(0x1e, 0x00) == "ld e, 0x00");
    CHECK(dis(0x1f) == "rra");
    CHECK(dis(0x20, 0x00) == "jr nz 0x8002");
    CHECK(dis(0x21, 0xff, 0xff) == "ld hl, 0xffff");
    CHECK(dis(0x22, 0x00, 0x00) == "ld (0x0000), hl");
    CHECK(dis(0x23) == "inc hl");
    CHECK(dis(0x24) == "inc h");
    CHECK(dis(0x25) == "dec h");
    CHECK(dis(0x26, 0x00) == "ld h, 0x00");
    CHECK(dis(0x27) == "daa");
    CHECK(dis(0x28, 0x80) == "jr z 0x7f82");
    CHECK(dis(0x2a, 0xef, 0xbe) == "ld hl, (0xbeef)");
    CHECK(dis(0x2b) == "dec hl");
    CHECK(dis(0x2c) == "inc l");
    CHECK(dis(0x2d) == "dec l");
    CHECK(dis(0x2e, 0x00) == "ld l, 0x00");
    CHECK(dis(0x2f) == "cpl");
    CHECK(dis(0x30, 0x00) == "jr nc 0x8002");
    CHECK(dis(0x31, 0x00, 0x00) == "ld sp, 0x0000");
    CHECK(dis(0x32, 0x00, 0x40) == "ld (0x4000), a");
    CHECK(dis(0x33) == "inc sp");
    CHECK(dis(0x34) == "inc (hl)");
    CHECK(dis(0x35) == "dec (hl)");
    CHECK(dis(0x36, 0x00) == "ld (hl), 0x00");
    CHECK(dis(0x37) == "scf");
    CHECK(dis(0x38, 0x00) == "jr c 0x8002");
    CHECK(dis(0x3a, 0xe3, 0x3e) == "ld a, (0x3ee3)");
    CHECK(dis(0x3b) == "dec sp");
    CHECK(dis(0x3c) == "inc a");
    CHECK(dis(0x3d) == "dec a");
    CHECK(dis(0x3e, 0x00) == "ld a, 0x00");
    CHECK(dis(0x3f) == "ccf");
    CHECK(dis(0x40) == "ld b, b");
    CHECK(dis(0x41) == "ld b, c");
    CHECK(dis(0x42) == "ld b, d");
    CHECK(dis(0x43) == "ld b, e");
    CHECK(dis(0x44) == "ld b, h");
    CHECK(dis(0x45) == "ld b, l");
    CHECK(dis(0x46) == "ld b, (hl)");
    CHECK(dis(0x47) == "ld b, a");
    CHECK(dis(0x48) == "ld c, b");
    CHECK(dis(0x49) == "ld c, c");
    CHECK(dis(0x4a) == "ld c, d");
    CHECK(dis(0x4b) == "ld c, e");
    CHECK(dis(0x4c) == "ld c, h");
    CHECK(dis(0x4d) == "ld c, l");
    CHECK(dis(0x4e) == "ld c, (hl)");
    CHECK(dis(0x4f) == "ld c, a");
    CHECK(dis(0x50) == "ld d, b");
    CHECK(dis(0x51) == "ld d, c");
    CHECK(dis(0x52) == "ld d, d");
    CHECK(dis(0x53) == "ld d, e");
    CHECK(dis(0x54) == "ld d, h");
    CHECK(dis(0x55) == "ld d, l");
    CHECK(dis(0x56) == "ld d, (hl)");
    CHECK(dis(0x57) == "ld d, a");
    CHECK(dis(0x58) == "ld e, b");
    CHECK(dis(0x59) == "ld e, c");
    CHECK(dis(0x5a) == "ld e, d");
    CHECK(dis(0x5b) == "ld e, e");
    CHECK(dis(0x5c) == "ld e, h");
    CHECK(dis(0x5d) == "ld e, l");
    CHECK(dis(0x5e) == "ld e, (hl)");
    CHECK(dis(0x5f) == "ld e, a");
    CHECK(dis(0x60) == "ld h, b");
    CHECK(dis(0x61) == "ld h, c");
    CHECK(dis(0x62) == "ld h, d");
    CHECK(dis(0x63) == "ld h, e");
    CHECK(dis(0x64) == "ld h, h");
    CHECK(dis(0x65) == "ld h, l");
    CHECK(dis(0x66) == "ld h, (hl)");
    CHECK(dis(0x67) == "ld h, a");
    CHECK(dis(0x68) == "ld l, b");
    CHECK(dis(0x69) == "ld l, c");
    CHECK(dis(0x6a) == "ld l, d");
    CHECK(dis(0x6b) == "ld l, e");
    CHECK(dis(0x6c) == "ld l, h");
    CHECK(dis(0x6d) == "ld l, l");
    CHECK(dis(0x6e) == "ld l, (hl)");
    CHECK(dis(0x6f) == "ld l, a");
    CHECK(dis(0x70) == "ld (hl), b");
    CHECK(dis(0x71) == "ld (hl), c");
    CHECK(dis(0x72) == "ld (hl), d");
    CHECK(dis(0x73) == "ld (hl), e");
    CHECK(dis(0x74) == "ld (hl), h");
    CHECK(dis(0x75) == "ld (hl), l");
    CHECK(dis(0x77) == "ld (hl), a");
    CHECK(dis(0x78) == "ld a, b");
    CHECK(dis(0x79) == "ld a, c");
    CHECK(dis(0x7a) == "ld a, d");
    CHECK(dis(0x7b) == "ld a, e");
    CHECK(dis(0x7c) == "ld a, h");
    CHECK(dis(0x7d) == "ld a, l");
    CHECK(dis(0x7e) == "ld a, (hl)");
    CHECK(dis(0x7f) == "ld a, a");
    CHECK(dis(0x80) == "add a, b");
    CHECK(dis(0x81) == "add a, c");
    CHECK(dis(0x82) == "add a, d");
    CHECK(dis(0x83) == "add a, e");
    CHECK(dis(0x84) == "add a, h");
    CHECK(dis(0x85) == "add a, l");
    CHECK(dis(0x86) == "add a, (hl)");
    CHECK(dis(0x87) == "add a, a");
    CHECK(dis(0x88) == "adc a, b");
    CHECK(dis(0x89) == "adc a, c");
    CHECK(dis(0x8a) == "adc a, d");
    CHECK(dis(0x8b) == "adc a, e");
    CHECK(dis(0x8c) == "adc a, h");
    CHECK(dis(0x8d) == "adc a, l");
    CHECK(dis(0x8e) == "adc a, (hl)");
    CHECK(dis(0x8f) == "adc a, a");
    CHECK(dis(0x90) == "sub a, b");
    CHECK(dis(0x91) == "sub a, c");
    CHECK(dis(0x92) == "sub a, d");
    CHECK(dis(0x93) == "sub a, e");
    CHECK(dis(0x94) == "sub a, h");
    CHECK(dis(0x95) == "sub a, l");
    CHECK(dis(0x96) == "sub a, (hl)");
    CHECK(dis(0x97) == "sub a, a");
    CHECK(dis(0x98) == "sbc a, b");
    CHECK(dis(0x99) == "sbc a, c");
    CHECK(dis(0x9a) == "sbc a, d");
    CHECK(dis(0x9b) == "sbc a, e");
    CHECK(dis(0x9c) == "sbc a, h");
    CHECK(dis(0x9d) == "sbc a, l");
    CHECK(dis(0x9e) == "sbc a, (hl)");
    CHECK(dis(0x9f) == "sbc a, a");
    CHECK(dis(0xa0) == "and b");
    CHECK(dis(0xa1) == "and c");
    CHECK(dis(0xa2) == "and d");
    CHECK(dis(0xa3) == "and e");
    CHECK(dis(0xa4) == "and h");
    CHECK(dis(0xa5) == "and l");
    CHECK(dis(0xa6) == "and (hl)");
    CHECK(dis(0xa7) == "and a");
    CHECK(dis(0xa8) == "xor b");
    CHECK(dis(0xa9) == "xor c");
    CHECK(dis(0xaa) == "xor d");
    CHECK(dis(0xab) == "xor e");
    CHECK(dis(0xac) == "xor h");
    CHECK(dis(0xad) == "xor l");
    CHECK(dis(0xae) == "xor (hl)");
    CHECK(dis(0xaf) == "xor a");
    CHECK(dis(0xb0) == "or b");
    CHECK(dis(0xb1) == "or c");
    CHECK(dis(0xb2) == "or d");
    CHECK(dis(0xb3) == "or e");
    CHECK(dis(0xb4) == "or h");
    CHECK(dis(0xb5) == "or l");
    CHECK(dis(0xb6) == "or (hl)");
    CHECK(dis(0xb7) == "or a");
    CHECK(dis(0xb8) == "cp b");
    CHECK(dis(0xb9) == "cp c");
    CHECK(dis(0xba) == "cp d");
    CHECK(dis(0xbb) == "cp e");
    CHECK(dis(0xbc) == "cp h");
    CHECK(dis(0xbd) == "cp l");
    CHECK(dis(0xbe) == "cp (hl)");
    CHECK(dis(0xbf) == "cp a");
    CHECK(dis(0xc0) == "ret nz");
    CHECK(dis(0xc1) == "pop bc");
    CHECK(dis(0xc2, 0x34, 0x12) == "jp nz, 0x1234");
    CHECK(dis(0xc3, 0x34, 0x12) == "jp 0x1234");
    CHECK(dis(0xc4, 0x34, 0x12) == "call nz, 0x1234");
    CHECK(dis(0xc5) == "push bc");
    CHECK(dis(0xc6, 0x00) == "add a, 0x00");
    CHECK(dis(0xc7) == "rst 0x00");
    CHECK(dis(0xc8) == "ret z");
    CHECK(dis(0xc9) == "ret");
    CHECK(dis(0xca, 0x34, 0x12) == "jp z, 0x1234");
    // CB prefix tested elsewhere
    CHECK(dis(0xcc, 0x34, 0x12) == "call z, 0x1234");
    CHECK(dis(0xcd, 0x34, 0x12) == "call 0x1234");
    CHECK(dis(0xce, 0x00) == "adc a, 0x00");
    CHECK(dis(0xcf) == "rst 0x08");
    CHECK(dis(0xd0) == "ret nc");
    CHECK(dis(0xd1) == "pop de");
    CHECK(dis(0xd2, 0x34, 0x12) == "jp nc, 0x1234");
    CHECK(dis(0xd3, 0x00) == "out (0x00), a");
    CHECK(dis(0xd4, 0x34, 0x12) == "call nc, 0x1234");
    CHECK(dis(0xd5) == "push de");
    CHECK(dis(0xd6, 0x00) == "sub a, 0x00");
    CHECK(dis(0xd7) == "rst 0x10");
    CHECK(dis(0xd8) == "ret c");
    CHECK(dis(0xd9) == "exx");
    CHECK(dis(0xda, 0x34, 0x12) == "jp c, 0x1234");
    CHECK(dis(0xdb, 0x00) == "in a, (0x00)");
    CHECK(dis(0xdc, 0x34, 0x12) == "call c, 0x1234");
    // DD tested elsewhere
    CHECK(dis(0xde, 0x00) == "sbc a, 0x00");
    CHECK(dis(0xdf) == "rst 0x18");
    CHECK(dis(0xe0) == "ret po");
    CHECK(dis(0xe1) == "pop hl");
    CHECK(dis(0xe2, 0x34, 0x12) == "jp po, 0x1234");
    CHECK(dis(0xe3) == "ex (sp), hl");
    CHECK(dis(0xe4, 0x34, 0x12) == "call po, 0x1234");
    CHECK(dis(0xe5) == "push hl");
    CHECK(dis(0xe6, 0x00) == "and 0x00");
    CHECK(dis(0xe7) == "rst 0x20");
    CHECK(dis(0xe8) == "ret pe");
    CHECK(dis(0xe9) == "jp (hl)");
    CHECK(dis(0xea, 0x34, 0x12) == "jp pe, 0x1234");
    CHECK(dis(0xeb) == "ex de, hl");
    CHECK(dis(0xec, 0x34, 0x12) == "call pe, 0x1234");
    // ED tested elsewhere
    CHECK(dis(0xee, 0x00) == "xor 0x00");
    CHECK(dis(0xef) == "rst 0x28");
    CHECK(dis(0xf0) == "ret p");
    CHECK(dis(0xf1) == "pop af");
    CHECK(dis(0xf2, 0x34, 0x12) == "jp p, 0x1234");
    CHECK(dis(0xf3) == "di");
    CHECK(dis(0xf4, 0x34, 0x12) == "call p, 0x1234");
    CHECK(dis(0xf5) == "push af");
    CHECK(dis(0xf6, 0x00) == "or 0x00");
    CHECK(dis(0xf7) == "rst 0x30");
    CHECK(dis(0xf8) == "ret m");
    CHECK(dis(0xf9) == "ld sp, hl");
    CHECK(dis(0xfa, 0x34, 0x12) == "jp m, 0x1234");
    CHECK(dis(0xfb) == "ei");
    CHECK(dis(0xfc, 0x34, 0x12) == "call m, 0x1234");
    // FD tested elsewhere
    CHECK(dis(0xfe, 0x00) == "cp 0x00");
    CHECK(dis(0xff) == "rst 0x38");
  }
  SECTION("Test CB prefix opcode disassembly") {
    CHECK(dis(0xcb, 0x00) == "rlc b");
    CHECK(dis(0xcb, 0x06) == "rlc (hl)");
    CHECK(dis(0xcb, 0x07) == "rlc a");
    CHECK(dis(0xcb, 0x08) == "rrc b");
    CHECK(dis(0xcb, 0x0e) == "rrc (hl)");
    CHECK(dis(0xcb, 0x0f) == "rrc a");
    CHECK(dis(0xcb, 0x10) == "rl b");
    CHECK(dis(0xcb, 0x16) == "rl (hl)");
    CHECK(dis(0xcb, 0x17) == "rl a");
    CHECK(dis(0xcb, 0x18) == "rr b");
    CHECK(dis(0xcb, 0x1e) == "rr (hl)");
    CHECK(dis(0xcb, 0x1f) == "rr a");
    CHECK(dis(0xcb, 0x20) == "sla b");
    CHECK(dis(0xcb, 0x26) == "sla (hl)");
    CHECK(dis(0xcb, 0x27) == "sla a");
    CHECK(dis(0xcb, 0x28) == "sra b");
    CHECK(dis(0xcb, 0x2e) == "sra (hl)");
    CHECK(dis(0xcb, 0x2f) == "sra a");
    CHECK(dis(0xcb, 0x30) == "sll b");
    CHECK(dis(0xcb, 0x36) == "sll (hl)");
    CHECK(dis(0xcb, 0x37) == "sll a");
    CHECK(dis(0xcb, 0x38) == "srl b");
    CHECK(dis(0xcb, 0x3e) == "srl (hl)");
    CHECK(dis(0xcb, 0x3f) == "srl a");
    CHECK(dis(0xcb, 0x40) == "bit 0, b");
    CHECK(dis(0xcb, 0x46) == "bit 0, (hl)");
    CHECK(dis(0xcb, 0x48) == "bit 1, b");
    CHECK(dis(0xcb, 0x4f) == "bit 1, a");
    CHECK(dis(0xcb, 0x7f) == "bit 7, a");
    CHECK(dis(0xcb, 0x80) == "res 0, b");
    CHECK(dis(0xcb, 0x86) == "res 0, (hl)");
    CHECK(dis(0xcb, 0x88) == "res 1, b");
    CHECK(dis(0xcb, 0x8f) == "res 1, a");
    CHECK(dis(0xcb, 0xbf) == "res 7, a");
    CHECK(dis(0xcb, 0xc0) == "set 0, b");
    CHECK(dis(0xcb, 0xc6) == "set 0, (hl)");
    CHECK(dis(0xcb, 0xc8) == "set 1, b");
    CHECK(dis(0xcb, 0xcf) == "set 1, a");
    CHECK(dis(0xcb, 0xff) == "set 7, a");
  }
  SECTION("Test dd prefixes") {
    CHECK(dis(0xdd, 0x04) == "inc b");
    CHECK(dis(0xdd, 0x36, 0x1f, 0x52) == "ld (ix+0x1f), 0x52");
    CHECK(dis(0xdd, 0x74, 0x02) == "ld (ix+0x02), h");
    CHECK(dis(0xdd, 0x6e, 0x02) == "ld l, (ix+0x02)");
    CHECK(dis(0xdd, 0x34, 0xff) == "inc (ix-0x01)");
    CHECK(dis(0xdd, 0x65) == "ld ixh, ixl");
    CHECK(dis(0xdd, 0xe9) == "jp (ix)");
    CHECK(dis(0xdd, 0xe5) == "push ix");
    CHECK(dis(0xdd, 0x09) == "add ix, bc");
    CHECK(dis(0xdd, 0x22, 0xad, 0xba) == "ld (0xbaad), ix");
  }
  SECTION("Test ddcb prefixes") {
    CHECK(dis(0xdd, 0xcb, 0xff, 0x06) == "rlc (ix-0x01)");
    CHECK(dis(0xdd, 0xcb, 0x23, 0xf6) == "set 6, (ix+0x23)");
  }
  SECTION("Test fdcb prefixes") {
    CHECK(dis(0xfd, 0xcb, 0xff, 0x06) == "rlc (iy-0x01)");
    CHECK(dis(0xfd, 0xcb, 0x23, 0xf6) == "set 6, (iy+0x23)");
  }
  SECTION("Test ed prefixes") {
    CHECK(dis(0xed, 0x40) == "in b, (c)");
    CHECK(dis(0xed, 0x41) == "out (c), b");
    CHECK(dis(0xed, 0x42) == "sbc hl, bc");
    CHECK(dis(0xed, 0x43, 0x34, 0x12) == "ld (0x1234), bc");
    CHECK(dis(0xed, 0x44) == "neg");
    CHECK(dis(0xed, 0x45) == "retn");
    CHECK(dis(0xed, 0x46) == "im 0");
    CHECK(dis(0xed, 0x47) == "ld i, a");
    CHECK(dis(0xed, 0x48) == "in c, (c)");
    CHECK(dis(0xed, 0x49) == "out (c), c");
    CHECK(dis(0xed, 0x4a) == "adc hl, bc");
    CHECK(dis(0xed, 0x4b, 0x34, 0x12) == "ld bc, (0x1234)");
    CHECK(dis(0xed, 0x4d) == "reti");
    CHECK(dis(0xed, 0x4f) == "ld r, a");
    CHECK(dis(0xed, 0x50) == "in d, (c)");
    CHECK(dis(0xed, 0x51) == "out (c), d");
    CHECK(dis(0xed, 0x52) == "sbc hl, de");
    CHECK(dis(0xed, 0x53, 0x34, 0x12) == "ld (0x1234), de");
    CHECK(dis(0xed, 0x56) == "im 1");
    CHECK(dis(0xed, 0x57) == "ld a, i");
    CHECK(dis(0xed, 0x58) == "in e, (c)");
    CHECK(dis(0xed, 0x59) == "out (c), e");
    CHECK(dis(0xed, 0x5a) == "adc hl, de");
    CHECK(dis(0xed, 0x5b, 0x34, 0x12) == "ld de, (0x1234)");
    CHECK(dis(0xed, 0x5e) == "im 2");
    CHECK(dis(0xed, 0x60) == "in h, (c)");
    CHECK(dis(0xed, 0x61) == "out (c), h");
    CHECK(dis(0xed, 0x62) == "sbc hl, hl");
    CHECK(dis(0xed, 0x63, 0x34, 0x12) == "ld (0x1234), hl");
    CHECK(dis(0xed, 0x67) == "rrd");
    CHECK(dis(0xed, 0x68) == "in l, (c)");
    CHECK(dis(0xed, 0x69) == "out (c), l");
    CHECK(dis(0xed, 0x6a) == "adc hl, hl");
    CHECK(dis(0xed, 0x6b, 0x34, 0x12) == "ld hl, (0x1234)");
    CHECK(dis(0xed, 0x6f) == "rld");
    CHECK(dis(0xed, 0x70) == "in (c)");
    CHECK(dis(0xed, 0x71) == "out (c), 0x00");
    CHECK(dis(0xed, 0x72) == "sbc hl, sp");
    CHECK(dis(0xed, 0x73, 0x34, 0x12) == "ld (0x1234), sp");
    CHECK(dis(0xed, 0x78) == "in a, (c)");
    CHECK(dis(0xed, 0x79) == "out (c), a");
    CHECK(dis(0xed, 0x7a) == "adc hl, sp");
    CHECK(dis(0xed, 0x7b, 0x34, 0x12) == "ld sp, (0x1234)");
    CHECK(dis(0xed, 0xa0) == "ldi");
    CHECK(dis(0xed, 0xa8) == "ldd");
    CHECK(dis(0xed, 0xb0) == "ldir");
    CHECK(dis(0xed, 0xb8) == "lddr");
    CHECK(dis(0xed, 0xa1) == "cpi");
    CHECK(dis(0xed, 0xa9) == "cpd");
    CHECK(dis(0xed, 0xb1) == "cpir");
    CHECK(dis(0xed, 0xb9) == "cpdr");
  }
}

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
    CHECK(is_indirect_for_testing()[opcode] == expected_but_backwards[255-opcode]);
  }
}

} // namespace specbolt
