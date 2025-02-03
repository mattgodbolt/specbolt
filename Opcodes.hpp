#pragma once

#include <array>
#include <string_view>

#include "Instruction.hpp"

namespace specbolt {

const Instruction &decode(std::uint8_t opcode, std::uint8_t nextOpcode, std::uint8_t nextNextOpcode);

constexpr std::array<std::string_view, 256> Opcodes = {"nop", "ld bc, nnnn", "ld (bc), a", "inc bc", "inc b", "dec b",
    "ld b, nn", "rlca", "ex af, af'", "add hl, bc", "ld a, (bc)", "dec bc", "inc c", "dec c", "ld c, nn", "rrca",
    "djnz offset", "ld de, nnnn", "ld (de), a", "inc de", "inc d", "dec d", "ld d, nn", "rla", "jr offset",
    "add hl, de", "ld a, (de)", "dec de", "inc e", "dec e", "ld e, nn", "rra", "jr nz, offset", "ld hl, nnnn",
    "ld (nnnn), hl", "inc hl", "inc h", "dec h", "ld h, nn", "daa", "jr z, offset", "add hl, hl", "ld hl, (nnnn)",
    "dec hl", "inc l", "dec l", "ld l, nn", "cpl", "jr nc, offset", "ld sp, nnnn", "ld (nnnn), a", "inc sp", "inc (hl)",
    "dec (hl)", "ld (hl), nn", "scf", "jr c, offset", "add hl, sp", "ld a, (nnnn)", "dec sp", "inc a", "dec a",
    "ld a, nn", "ccf", "ld b, b", "ld b, c", "ld b, d", "ld b, e", "ld b, h", "ld b, l", "ld b, (hl)", "ld b, a",
    "ld c, b", "ld c, c", "ld c, d", "ld c, e", "ld c, h", "ld c, l", "ld c, (hl)", "ld c, a", "ld d, b", "ld d, c",
    "ld d, d", "ld d, e", "ld d, h", "ld d, l", "ld d, (hl)", "ld d, a", "ld e, b", "ld e, c", "ld e, d", "ld e, e",
    "ld e, h", "ld e, l", "ld e, (hl)", "ld e, a", "ld h, b", "ld h, c", "ld h, d", "ld h, e", "ld h, h", "ld h, l",
    "ld h, (hl)", "ld h, a", "ld l, b", "ld l, c", "ld l, d", "ld l, e", "ld l, h", "ld l, l", "ld l, (hl)", "ld l, a",
    "ld (hl), b", "ld (hl), c", "ld (hl), d", "ld (hl), e", "ld (hl), h", "ld (hl), l", "halt", "ld (hl), a", "ld a, b",
    "ld a, c", "ld a, d", "ld a, e", "ld a, h", "ld a, l", "ld a, (hl)", "ld a, a", "add a, b", "add a, c", "add a, d",
    "add a, e", "add a, h", "add a, l", "add a, (hl)", "add a, a", "adc a, b", "adc a, c", "adc a, d", "adc a, e",
    "adc a, h", "adc a, l", "adc a, (hl)", "adc a, a", "sub a, b", "sub a, c", "sub a, d", "sub a, e", "sub a, h",
    "sub a, l", "sub a, (hl)", "sub a, a", "sbc a, b", "sbc a, c", "sbc a, d", "sbc a, e", "sbc a, h", "sbc a, l",
    "sbc a, (hl)", "sbc a, a", "and a, b", "and a, c", "and a, d", "and a, e", "and a, h", "and a, l", "and a, (hl)",
    "and a, a", "xor a, b", "xor a, c", "xor a, d", "xor a, e", "xor a, h", "xor a, l", "xor a, (hl)", "xor a, a",
    "or a, b", "or a, c", "or a, d", "or a, e", "or a, h", "or a, l", "or a, (hl)", "or a, a", "cp b", "cp c", "cp d",
    "cp e", "cp h", "cp l", "cp (hl)", "cp a", "ret nz", "pop bc", "jp nz, nnnn", "jp nnnn", "call nz, nnnn", "push bc",
    "add a, nn", "rst 00", "ret z", "ret", "jp z, nnnn", "shift cb", "call z, nnnn", "call nnnn", "adc a, nn", "rst 8",
    "ret nc", "pop de", "jp nc, nnnn", "out (nn), a", "call nc, nnnn", "push de", "sub nn", "rst 10", "ret c", "exx",
    "jp c, nnnn", "in a, (nn)", "call c, nnnn", "shift dd", "sbc a, nn", "rst 18", "ret po", "pop hl", "jp po, nnnn",
    "ex (sp), hl", "call po, nnnn", "push hl", "and nn", "rst 20", "ret pe", "jp hl", "jp pe, nnnn", "ex de, hl",
    "call pe, nnnn", "shift ed", "xor a, nn", "rst 28", "ret p", "pop af", "jp p, nnnn", "di", "call p, nnnn",
    "push af", "or nn", "rst 30", "ret m", "ld sp, hl", "jp m, nnnn", "ei", "call m, nnnn", "shift fd", "cp nn",
    "rst 38"};

constexpr std::array<std::string_view, 256> OpcodesCB = {
    "rlc b",
    "rlc c",
    "rlc d",
    "rlc e",
    "rlc h",
    "rlc l",
    "rlc (hl)",
    "rlc a",
    "rrc b",
    "rrc c",
    "rrc d",
    "rrc e",
    "rrc h",
    "rrc l",
    "rrc (hl)",
    "rrc a",
    "rl b",
    "rl c",
    "rl d",
    "rl e",
    "rl h",
    "rl l",
    "rl (hl)",
    "rl a",
    "rr b",
    "rr c",
    "rr d",
    "rr e",
    "rr h",
    "rr l",
    "rr (hl)",
    "rr a",
    "sla b",
    "sla c",
    "sla d",
    "sla e",
    "sla h",
    "sla l",
    "sla (hl)",
    "sla a",
    "sra b",
    "sra c",
    "sra d",
    "sra e",
    "sra h",
    "sra l",
    "sra (hl)",
    "sra a",
    "sll b",
    "sll c",
    "sll d",
    "sll e",
    "sll h",
    "sll l",
    "sll (hl)",
    "sll a",
    "srl b",
    "srl c",
    "srl d",
    "srl e",
    "srl h",
    "srl l",
    "srl (hl)",
    "srl a",
    "bit 0, b",
    "bit 0, c",
    "bit 0, d",
    "bit 0, e",
    "bit 0, h",
    "bit 0, l",
    "bit 0, (hl)",
    "bit 0, a",
    "bit 1, b",
    "bit 1, c",
    "bit 1, d",
    "bit 1, e",
    "bit 1, h",
    "bit 1, l",
    "bit 1, (hl)",
    "bit 1, a",
    "bit 2, b",
    "bit 2, c",
    "bit 2, d",
    "bit 2, e",
    "bit 2, h",
    "bit 2, l",
    "bit 2, (hl)",
    "bit 2, a",
    "bit 3, b",
    "bit 3, c",
    "bit 3, d",
    "bit 3, e",
    "bit 3, h",
    "bit 3, l",
    "bit 3, (hl)",
    "bit 3, a",
    "bit 4, b",
    "bit 4, c",
    "bit 4, d",
    "bit 4, e",
    "bit 4, h",
    "bit 4, l",
    "bit 4, (hl)",
    "bit 4, a",
    "bit 5, b",
    "bit 5, c",
    "bit 5, d",
    "bit 5, e",
    "bit 5, h",
    "bit 5, l",
    "bit 5, (hl)",
    "bit 5, a",
    "bit 6, b",
    "bit 6, c",
    "bit 6, d",
    "bit 6, e",
    "bit 6, h",
    "bit 6, l",
    "bit 6, (hl)",
    "bit 6, a",
    "bit 7, b",
    "bit 7, c",
    "bit 7, d",
    "bit 7, e",
    "bit 7, h",
    "bit 7, l",
    "bit 7, (hl)",
    "bit 7, a",
    "res 0, b",
    "res 0, c",
    "res 0, d",
    "res 0, e",
    "res 0, h",
    "res 0, l",
    "res 0, (hl)",
    "res 0, a",
    "res 1, b",
    "res 1, c",
    "res 1, d",
    "res 1, e",
    "res 1, h",
    "res 1, l",
    "res 1, (hl)",
    "res 1, a",
    "res 2, b",
    "res 2, c",
    "res 2, d",
    "res 2, e",
    "res 2, h",
    "res 2, l",
    "res 2, (hl)",
    "res 2, a",
    "res 3, b",
    "res 3, c",
    "res 3, d",
    "res 3, e",
    "res 3, h",
    "res 3, l",
    "res 3, (hl)",
    "res 3, a",
    "res 4, b",
    "res 4, c",
    "res 4, d",
    "res 4, e",
    "res 4, h",
    "res 4, l",
    "res 4, (hl)",
    "res 4, a",
    "res 5, b",
    "res 5, c",
    "res 5, d",
    "res 5, e",
    "res 5, h",
    "res 5, l",
    "res 5, (hl)",
    "res 5, a",
    "res 6, b",
    "res 6, c",
    "res 6, d",
    "res 6, e",
    "res 6, h",
    "res 6, l",
    "res 6, (hl)",
    "res 6, a",
    "res 7, b",
    "res 7, c",
    "res 7, d",
    "res 7, e",
    "res 7, h",
    "res 7, l",
    "res 7, (hl)",
    "res 7, a",
    "set 0, b",
    "set 0, c",
    "set 0, d",
    "set 0, e",
    "set 0, h",
    "set 0, l",
    "set 0, (hl)",
    "set 0, a",
    "set 1, b",
    "set 1, c",
    "set 1, d",
    "set 1, e",
    "set 1, h",
    "set 1, l",
    "set 1, (hl)",
    "set 1, a",
    "set 2, b",
    "set 2, c",
    "set 2, d",
    "set 2, e",
    "set 2, h",
    "set 2, l",
    "set 2, (hl)",
    "set 2, a",
    "set 3, b",
    "set 3, c",
    "set 3, d",
    "set 3, e",
    "set 3, h",
    "set 3, l",
    "set 3, (hl)",
    "set 3, a",
    "set 4, b",
    "set 4, c",
    "set 4, d",
    "set 4, e",
    "set 4, h",
    "set 4, l",
    "set 4, (hl)",
    "set 4, a",
    "set 5, b",
    "set 5, c",
    "set 5, d",
    "set 5, e",
    "set 5, h",
    "set 5, l",
    "set 5, (hl)",
    "set 5, a",
    "set 6, b",
    "set 6, c",
    "set 6, d",
    "set 6, e",
    "set 6, h",
    "set 6, l",
    "set 6, (hl)",
    "set 6, a",
    "set 7, b",
    "set 7, c",
    "set 7, d",
    "set 7, e",
    "set 7, h",
    "set 7, l",
    "set 7, (hl)",
    "set 7, a",
};

constexpr std::string_view GetOpcodeDD(const std::uint8_t opcode) {
  switch (opcode) {
    default:
      return "??";
    case 0xcb:
      return "??"; // specifically not supported...
    case 0x09:
      return "add ix, bc";
    case 0x19:
      return "add ix, de";
    case 0x21:
      return "ld ix, nnnn";
    case 0x22:
      return "ld (nnnn), ix";
    case 0x23:
      return "inc ix";
    case 0x24:
      return "inc ixh";
    case 0x25:
      return "dec ixh";
    case 0x26:
      return "ld ixh, nn";
    case 0x29:
      return "add ix, ix";
    case 0x2a:
      return "ld ix, (nnnn)";
    case 0x2b:
      return "dec ix";
    case 0x2c:
      return "inc ixl";
    case 0x2d:
      return "dec ixl";
    case 0x2e:
      return "ld ixl, nn";
    case 0x34:
      return "inc (ix+nn)";
    case 0x35:
      return "dec (ix+nn)";
    case 0x36:
      return "ld (ix+nn), nn";
    case 0x39:
      return "add ix, sp";
    case 0x44:
      return "ld b, ixh";
    case 0x45:
      return "ld b, ixl";
    case 0x46:
      return "ld b, (ix+nn)";
    case 0x4c:
      return "ld c, ixh";
    case 0x4d:
      return "ld c, ixl";
    case 0x4e:
      return "ld c, (ix+nn)";
    case 0x54:
      return "ld d, ixh";
    case 0x55:
      return "ld d, ixl";
    case 0x56:
      return "ld d, (ix+nn)";
    case 0x5c:
      return "ld e, ixh";
    case 0x5d:
      return "ld e, ixl";
    case 0x5e:
      return "ld e, (ix+nn)";
    case 0x60:
      return "ld ixh, b";
    case 0x61:
      return "ld ixh, c";
    case 0x62:
      return "ld ixh, d";
    case 0x63:
      return "ld ixh, e";
    case 0x64:
      return "ld ixh, ixh";
    case 0x65:
      return "ld ixh, ixl";
    case 0x66:
      return "ld h, (ix+nn)";
    case 0x67:
      return "ld ixh, a";
    case 0x68:
      return "ld ixl, b";
    case 0x69:
      return "ld ixl, c";
    case 0x6a:
      return "ld ixl, d";
    case 0x6b:
      return "ld ixl, e";
    case 0x6c:
      return "ld ixl, ixh";
    case 0x6d:
      return "ld ixl, ixl";
    case 0x6e:
      return "ld l, (ix+nn)";
    case 0x6f:
      return "ld ixl, a";
    case 0x70:
      return "ld (ix+nn), b";
    case 0x71:
      return "ld (ix+nn), c";
    case 0x72:
      return "ld (ix+nn), d";
    case 0x73:
      return "ld (ix+nn), e";
    case 0x74:
      return "ld (ix+nn), h";
    case 0x75:
      return "ld (ix+nn), l";
    case 0x77:
      return "ld (ix+nn), a";
    case 0x7c:
      return "ld a, ixh";
    case 0x7d:
      return "ld a, ixl";
    case 0x7e:
      return "ld a, (ix+nn)";
    case 0x84:
      return "add a, ixh";
    case 0x85:
      return "add a, ixl";
    case 0x86:
      return "add a, (ix+nn)";
    case 0x8c:
      return "adc a, ixh";
    case 0x8d:
      return "adc a, ixl";
    case 0x8e:
      return "adc a, (ix+nn)";
    case 0x94:
      return "sub a, ixh";
    case 0x95:
      return "sub a, ixl";
    case 0x96:
      return "sub a, (ix+nn)";
    case 0x9c:
      return "sbc a, ixh";
    case 0x9d:
      return "sbc a, ixl";
    case 0x9e:
      return "sbc a, (ix+nn)";
    case 0xa4:
      return "and a, ixh";
    case 0xa5:
      return "and a, ixl";
    case 0xa6:
      return "and a, (ix+nn)";
    case 0xac:
      return "xor a, ixh";
    case 0xad:
      return "xor a, ixl";
    case 0xae:
      return "xor a, (ix+nn)";
    case 0xb4:
      return "or a, ixh";
    case 0xb5:
      return "or a, ixl";
    case 0xb6:
      return "or a, (ix+nn)";
    case 0xbc:
      return "cp a, ixh";
    case 0xbd:
      return "cp a, ixl";
    case 0xbe:
      return "cp a, (ix+nn)";
    case 0xe1:
      return "pop ix";
    case 0xe3:
      return "ex (sp), ix";
    case 0xe5:
      return "push ix";
    case 0xe9:
      return "jp ix";
    case 0xf9:
      return "ld sp, ix";
  }
}

constexpr std::string_view GetOpcodeFD(const std::uint8_t opcode) {
  switch (opcode) {
    default:
      return "??";
    case 0xcb:
      return "??"; // specifically not supported...
    case 0x09:
      return "add iy, bc";
    case 0x19:
      return "add iy, de";
    case 0x21:
      return "ld iy, nnnn";
    case 0x22:
      return "ld (nnnn), iy";
    case 0x23:
      return "inc iy";
    case 0x24:
      return "inc iyh";
    case 0x25:
      return "dec iyh";
    case 0x26:
      return "ld iyh, nn";
    case 0x29:
      return "add iy, iy";
    case 0x2a:
      return "ld iy, (nnnn)";
    case 0x2b:
      return "dec iy";
    case 0x2c:
      return "inc iyl";
    case 0x2d:
      return "dec iyl";
    case 0x2e:
      return "ld iyl, nn";
    case 0x34:
      return "inc (iy+nn)";
    case 0x35:
      return "dec (iy+nn)";
    case 0x36:
      return "ld (iy+nn), nn";
    case 0x39:
      return "add iy, sp";
    case 0x44:
      return "ld b, iyh";
    case 0x45:
      return "ld b, iyl";
    case 0x46:
      return "ld b, (iy+nn)";
    case 0x4c:
      return "ld c, iyh";
    case 0x4d:
      return "ld c, iyl";
    case 0x4e:
      return "ld c, (iy+nn)";
    case 0x54:
      return "ld d, iyh";
    case 0x55:
      return "ld d, iyl";
    case 0x56:
      return "ld d, (iy+nn)";
    case 0x5c:
      return "ld e, iyh";
    case 0x5d:
      return "ld e, iyl";
    case 0x5e:
      return "ld e, (iy+nn)";
    case 0x60:
      return "ld iyh, b";
    case 0x61:
      return "ld iyh, c";
    case 0x62:
      return "ld iyh, d";
    case 0x63:
      return "ld iyh, e";
    case 0x64:
      return "ld iyh, iyh";
    case 0x65:
      return "ld iyh, iyl";
    case 0x66:
      return "ld h, (iy+nn)";
    case 0x67:
      return "ld iyh, a";
    case 0x68:
      return "ld iyl, b";
    case 0x69:
      return "ld iyl, c";
    case 0x6a:
      return "ld iyl, d";
    case 0x6b:
      return "ld iyl, e";
    case 0x6c:
      return "ld iyl, iyh";
    case 0x6d:
      return "ld iyl, iyl";
    case 0x6e:
      return "ld l, (iy+nn)";
    case 0x6f:
      return "ld iyl, a";
    case 0x70:
      return "ld (iy+nn), b";
    case 0x71:
      return "ld (iy+nn), c";
    case 0x72:
      return "ld (iy+nn), d";
    case 0x73:
      return "ld (iy+nn), e";
    case 0x74:
      return "ld (iy+nn), h";
    case 0x75:
      return "ld (iy+nn), l";
    case 0x77:
      return "ld (iy+nn), a";
    case 0x7c:
      return "ld a, iyh";
    case 0x7d:
      return "ld a, iyl";
    case 0x7e:
      return "ld a, (iy+nn)";
    case 0x84:
      return "add a, iyh";
    case 0x85:
      return "add a, iyl";
    case 0x86:
      return "add a, (iy+nn)";
    case 0x8c:
      return "adc a, iyh";
    case 0x8d:
      return "adc a, iyl";
    case 0x8e:
      return "adc a, (iy+nn)";
    case 0x94:
      return "sub a, iyh";
    case 0x95:
      return "sub a, iyl";
    case 0x96:
      return "sub a, (iy+nn)";
    case 0x9c:
      return "sbc a, iyh";
    case 0x9d:
      return "sbc a, iyl";
    case 0x9e:
      return "sbc a, (iy+nn)";
    case 0xa4:
      return "and a, iyh";
    case 0xa5:
      return "and a, iyl";
    case 0xa6:
      return "and a, (iy+nn)";
    case 0xac:
      return "xor a, iyh";
    case 0xad:
      return "xor a, iyl";
    case 0xae:
      return "xor a, (iy+nn)";
    case 0xb4:
      return "or a, iyh";
    case 0xb5:
      return "or a, iyl";
    case 0xb6:
      return "or a, (iy+nn)";
    case 0xbc:
      return "cp a, iyh";
    case 0xbd:
      return "cp a, iyl";
    case 0xbe:
      return "cp a, (iy+nn)";
    case 0xe1:
      return "pop iy";
    case 0xe3:
      return "ex (sp), iy";
    case 0xe5:
      return "push iy";
    case 0xe9:
      return "jp iy";
    case 0xf9:
      return "ld sp, iy";
  }
}

constexpr std::string_view GetOpcodeED(const std::uint8_t opcode) {
  switch (opcode) {
    default:
      return "??";
    case 0x40:
      return "in b, (c)";
    case 0x41:
      return "out (c), b";
    case 0x42:
      return "sbc hl, bc";
    case 0x43:
      return "ld (nnnn), bc";
    case 0x44:
    case 0x4c:
    case 0x54:
    case 0x5c:
    case 0x64:
    case 0x6c:
    case 0x74:
    case 0x7c:
      return "neg";
    case 0x45:
    case 0x4d:
    case 0x55:
    case 0x5d:
    case 0x65:
    case 0x6d:
    case 0x75:
    case 0x7d:
      return "retn";
    case 0x46:
    case 0x4e:
    case 0x66:
    case 0x6e:
      return "im 0";
    case 0x47:
      return "ld i, a";
    case 0x48:
      return "in c, (c)";
    case 0x49:
      return "out (c), c";
    case 0x4a:
      return "adc hl, bc";
    case 0x4b:
      return "ld bc, (nnnn)";
    case 0x4f:
      return "ld r, a";
    case 0x50:
      return "in d, (c)";
    case 0x51:
      return "out (c), d";
    case 0x52:
      return "sbc hl, de";
    case 0x53:
      return "ld (nnnn), de";
    case 0x56:
    case 0x76:
      return "im 1";
    case 0x57:
      return "ld a, i";
    case 0x58:
      return "in e, (c)";
    case 0x59:
      return "out (c), e";
    case 0x5a:
      return "adc hl, de";
    case 0x5b:
      return "ld de, (nnnn)";
    case 0x5e:
    case 0x7e:
      return "im 2";
    case 0x5f:
      return "ld a, r";
    case 0x60:
      return "in h, (c)";
    case 0x61:
      return "out (c), h";
    case 0x62:
      return "sbc hl, hl";
    case 0x63:
      return "ld (nnnn), hl";
    case 0x67:
      return "rrd";
    case 0x68:
      return "in l, (c)";
    case 0x69:
      return "out (c), l";
    case 0x6a:
      return "adc hl, hl";
    case 0x6b:
      return "ld hl, (nnnn)";
    case 0x6f:
      return "rld";
    case 0x70:
      return "in f, (c)";
    case 0x71:
      return "out (c), 0";
    case 0x72:
      return "sbc hl, sp";
    case 0x73:
      return "ld (nnnn), sp";
    case 0x78:
      return "in a, (c)";
    case 0x79:
      return "out (c), a";
    case 0x7a:
      return "adc hl, sp";
    case 0x7b:
      return "ld sp, (nnnn)";
    case 0xa0:
      return "ldi";
    case 0xa1:
      return "cpi";
    case 0xa2:
      return "ini";
    case 0xa3:
      return "outi";
    case 0xa8:
      return "ldd";
    case 0xa9:
      return "cpd";
    case 0xaa:
      return "ind";
    case 0xab:
      return "outd";
    case 0xb0:
      return "ldir";
    case 0xb1:
      return "cpir";
    case 0xb2:
      return "inir";
    case 0xb3:
      return "otir";
    case 0xb8:
      return "lddr";
    case 0xb9:
      return "cpdr";
    case 0xba:
      return "indr";
    case 0xbb:
      return "otdr";
  }
}

} // namespace specbolt
