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
  }
  SECTION("Test base opcode operations") {
    const auto &ops = base_opcode_ops();
    Memory memory;
    memory.set_rom_size(0);
    Z80 z80{memory};
    auto run_one_instruction = [&](auto... bytes) {
      for (auto &&[index, val]: std::views::enumerate(std::array{bytes...})) {
        memory.write(static_cast<std::uint16_t>(z80.pc() + index), static_cast<std::uint8_t>(val));
      }
      // simulate fetch
      const auto opcode = z80.read8(z80.pc());
      z80.regs().pc(z80.pc() + 1);
      z80.pass_time(4);
      ops[opcode](z80);
    };

    SECTION("LD bc, nnnn") {
      run_one_instruction(0x01, 0x34, 0x12);
      CHECK(z80.regs().get(RegisterFile::R16::BC) == 0x1234);
      CHECK(z80.pc() == 3);
      CHECK(z80.cycle_count() == 10);
    }
  }
}

} // namespace specbolt
