#include "../Disassembler.hpp"

#include <catch2/catch_test_macros.hpp>

using specbolt::Disassembler;

TEST_CASE("Disassembler tests") {
  std::array<std::uint8_t, 65536> memory;
  Disassembler dis(memory);
  SECTION("NOPs") {
    SECTION("at address 0") {
      const auto instr = dis.disassemble(0);
      CHECK(instr.address == 0);
      CHECK(instr.length == 1);
      CHECK(instr.bytes == std::array<std::uint8_t, 4>{0, 0, 0, 0});
      CHECK(instr.to_string() == "0000  00        nop");
    }
    SECTION("at address 123") {
      const auto instr = dis.disassemble(0x123);
      CHECK(instr.address == 0x123);
      CHECK(instr.length == 1);
      CHECK(instr.bytes == std::array<std::uint8_t, 4>{0, 0, 0, 0});
      CHECK(instr.to_string() == "0123  00        nop");
    }
  }
  SECTION("16-bit operands") {
    memory[0x200] = 0x22; // LD (nnnn), HL
    memory[0x201] = 0x5f;
    memory[0x202] = 0x5c;
    const auto instr = dis.disassemble(0x200);
    CHECK(instr.length == 3);
    CHECK(instr.to_string() == "0200  22 5f 5c  ld (0x5c5f),hl");
  }
}
