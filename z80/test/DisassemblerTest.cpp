#include "z80/Disassembler.hpp"

#include "peripherals//Memory.hpp"
#include "z80/Instruction.hpp"

#include <catch2/catch_test_macros.hpp>

using specbolt::Disassembler;
using specbolt::Memory;

TEST_CASE("Disassembler tests") {
  Memory memory;
  Disassembler dis(memory);
  SECTION("NOPs") {
    SECTION("at address 0") {
      const auto instr = dis.disassemble(0);
      CHECK(instr.address == 0);
      CHECK(instr.instruction.length == 1);
      CHECK(instr.bytes == std::array<std::uint8_t, 4>{0, 0, 0, 0});
      CHECK(instr.to_string() == "0000  00           nop");
    }
    SECTION("at address 123") {
      const auto instr = dis.disassemble(0x123);
      CHECK(instr.address == 0x123);
      CHECK(instr.instruction.length == 1);
      CHECK(instr.bytes == std::array<std::uint8_t, 4>{0, 0, 0, 0});
      CHECK(instr.to_string() == "0123  00           nop");
    }
  }
  SECTION("16-bit operands") {
    memory.raw_write(0x200, 0x22); // LD (nnnn), HL
    memory.raw_write(0x201, 0x5f);
    memory.raw_write(0x202, 0x5c);
    const auto instr = dis.disassemble(0x200);
    CHECK(instr.instruction.length == 3);
    CHECK(instr.to_string() == "0200  22 5f 5c     ld (0x5c5f), hl");
  }
  SECTION("8-bit operands") {
    memory.raw_write(0x200, 0x3e); // LD a, nn
    memory.raw_write(0x201, 0xbd);
    const auto instr = dis.disassemble(0x200);
    CHECK(instr.instruction.length == 2);
    CHECK(instr.to_string() == "0200  3e bd        ld a, 0xbd");
  }
  SECTION("Offset operands") {
    SECTION("forward") {
      memory.raw_write(0xe, 0x18); // jr offset
      memory.raw_write(0xf, 0x43);
      const auto instr = dis.disassemble(0xe);
      CHECK(instr.instruction.length == 2);
      CHECK(instr.to_string() == "000e  18 43        jr 0x0053");
    }
    SECTION("backward") {
      memory.raw_write(0x23, 0x18); // jr offset
      memory.raw_write(0x24, 0xf7);
      const auto instr = dis.disassemble(0x23);
      CHECK(instr.instruction.length == 2);
      CHECK(instr.to_string() == "0023  18 f7        jr 0x001c");
    }
  }
  SECTION("Indexed") {
    memory.raw_write(0x55, 0xfd); // prefix
    memory.raw_write(0x56, 0x75); // ld (iy), l
    memory.raw_write(0x57, 0x12);
    const auto instr = dis.disassemble(0x55);
    CHECK(instr.instruction.length == 3);
    CHECK(instr.to_string() == "0055  fd 75 12     ld (iy+0x12), l");
  }
  SECTION("Extended with offset") {
    memory.raw_write(0x58, 0xed); // prefix
    memory.raw_write(0x59, 0x7b); // ld sp, (nnnn)
    memory.raw_write(0x5a, 0x3d);
    memory.raw_write(0x5b, 0x5c);
    const auto instr = dis.disassemble(0x58);
    CHECK(instr.instruction.length == 4);
    CHECK(instr.to_string() == "0058  ed 7b 3d 5c  ld sp, (0x5c3d)");
  }
  SECTION("cb prefixes") {
    memory.raw_write(0x270, 0xcb); // prefix
    memory.raw_write(0x271, 0x5e); // bit 3. (hl)
    const auto instr = dis.disassemble(0x270);
    CHECK(instr.instruction.length == 2);
    CHECK(instr.to_string() == "0270  cb 5e        bit 3, (hl)");
  }
  SECTION("cb prefixes") {
    memory.raw_write(0x270, 0xcb); // prefix
    memory.raw_write(0x271, 0x5e); // bit 3. (hl)
    const auto instr = dis.disassemble(0x270);
    CHECK(instr.instruction.length == 2);
    CHECK(instr.to_string() == "0270  cb 5e        bit 3, (hl)");
  }
  SECTION("fd prefixes") {
    SECTION("without constant") {
      memory.raw_write(0x1276, 0xfd); // prefix
      memory.raw_write(0x1277, 0x35); // dec []
      memory.raw_write(0x1278, 0xc6); // -0x3a
      const auto instr = dis.disassemble(0x1276);
      CHECK(instr.instruction.length == 3);
      CHECK(instr.to_string() == "1276  fd 35 c6     dec (iy-0x3a)");
    }
    SECTION("with constant") {
      memory.raw_write(0x128e, 0xfd); // prefix
      memory.raw_write(0x128f, 0x36); // ld [], nn
      memory.raw_write(0x1290, 0x31); // +0x31
      memory.raw_write(0x1291, 0x02); // 0x02
      const auto instr = dis.disassemble(0x128e);
      CHECK(instr.instruction.length == 4);
      CHECK(instr.to_string() == "128e  fd 36 31 02  ld (iy+0x31), 0x02");
    }
  }
  SECTION("fdcb prefixes") {
    memory.raw_write(0x1287, 0xfd); // fd prefix (iy+nn)
    memory.raw_write(0x1288, 0xcb); // cb prefix (bit operations)
    memory.raw_write(0x1289, 0x01); // offset +0x01
    memory.raw_write(0x128a, 0xce); // set 1, [...]
    const auto instr = dis.disassemble(0x1287);
    CHECK(instr.instruction.length == 4);
    CHECK(instr.to_string() == "1287  fd cb 01 ce  set 1, (iy+0x01)");
  }
  SECTION("ld h, (ix+nn)") {
    memory.raw_write(0x0000, 0xdd); // dd prefix
    memory.raw_write(0x0001, 0x66); // ld h, (ix+nn)
    memory.raw_write(0x0002, 0x01); // offset +0x01
    const auto instr = dis.disassemble(0);
    CHECK(instr.instruction.length == 3);
    CHECK(instr.to_string() == "0000  dd 66 01     ld h, (ix+0x01)");
  }
  SECTION("ld iyh, nn") {
    memory.raw_write(0x0000, 0xfd); // fd prefix
    memory.raw_write(0x0001, 0x26); // ld iyh, nn
    memory.raw_write(0x0002, 0xde); // 0xde
    const auto instr = dis.disassemble(0);
    CHECK(instr.instruction.length == 3);
    CHECK(instr.to_string() == "0000  fd 26 de     ld iyh, 0xde");
  }
  SECTION("ex (sp), iy") {
    memory.raw_write(0x0000, 0xfd); // fd prefix
    memory.raw_write(0x0001, 0xe3); // ex (sp), iy
    const auto instr = dis.disassemble(0);
    CHECK(instr.instruction.length == 2);
    CHECK(instr.to_string() == "0000  fd e3        ex (sp), iy");
  }
  // TODO the other prefix bytes... cb dd ed fd, bit prefixes
}
