
#include "peripherals/Memory.hpp"
#include "z80/Generated.hpp"
#include "z80/Z80.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdint>
#include <ranges>

namespace specbolt {

TEST_CASE("Opcode execution tests") {
  Memory memory;
  memory.set_rom_size(0);
  Z80 z80{memory};
  auto &regs = z80.regs();
  const auto use_new_code = GENERATE(false, true);
  const auto name = use_new_code ? "Using new code" : "Using old code";
  INFO(name);
  auto run_one_instruction = [&](auto... bytes) {
    for (auto &&[index, val]: std::views::enumerate(std::array{bytes...})) {
      memory.write(static_cast<std::uint16_t>(z80.pc() + index), static_cast<std::uint8_t>(val));
    }
    if (use_new_code) {
      // simulate fetch
      const auto opcode = z80.read8(z80.pc());
      regs.pc(z80.pc() + 1);
      z80.pass_time(4);
      // Dispatch and run.
      base_opcode_ops()[opcode](z80);
    }
    else {
      z80.execute_one();
    }
  };

  SECTION("ld bc, nnnn") {
    run_one_instruction(0x01, 0x34, 0x12);
    CHECK(regs.get(RegisterFile::R16::BC) == 0x1234);
    CHECK(z80.pc() == 3);
    CHECK(z80.cycle_count() == 10);
  }
  SECTION("ld sp, nnnn") {
    run_one_instruction(0x31, 0x34, 0x12);
    CHECK(regs.sp() == 0x1234);
    CHECK(z80.pc() == 3);
    CHECK(z80.cycle_count() == 10);
  }
  SECTION("djnz") {
    SECTION("not taken") {
      regs.set(RegisterFile::R8::B, 1);
      run_one_instruction(0x10, 0x03); // djnz +3
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 8);
    }
    SECTION("taken") {
      regs.set(RegisterFile::R8::B, 0);
      run_one_instruction(0x10, 0x44); // djnz +44
      CHECK(z80.pc() == 0x46);
      CHECK(z80.cycle_count() == 13);
    }
  }
  SECTION("ex af, af'") {
    regs.set(RegisterFile::R16::AF, 1234);
    regs.set(RegisterFile::R16::AF_, 4321);
    run_one_instruction(0x08);
    CHECK(z80.pc() == 0x01);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R16::AF) == 4321);
    CHECK(regs.get(RegisterFile::R16::AF_) == 1234);
  }
  SECTION("jr") {
    run_one_instruction(0x18, 0xfe); // jr -2  ; self
    CHECK(z80.pc() == 0x00);
    CHECK(z80.cycle_count() == 12);
  }
  SECTION("jr nz") {
    SECTION("taken") {
      regs.set(RegisterFile::R8::F, Flags().to_u8());
      run_one_instruction(0x20, 0xfe); // jr nz -2  ; self
      CHECK(z80.pc() == 0x00);
      CHECK(z80.cycle_count() == 12);
    }
    SECTION("not taken") {
      regs.set(RegisterFile::R8::F, Flags::Zero().to_u8());
      run_one_instruction(0x20, 0xfe); // jr nz -2  ; self
      CHECK(z80.pc() == 0x02);
      CHECK(z80.cycle_count() == 7);
    }
  }
  SECTION("jr z") {
    SECTION("taken") {
      regs.set(RegisterFile::R8::F, Flags::Zero().to_u8());
      run_one_instruction(0x28, 0xfe); // jr z -2  ; self
      CHECK(z80.pc() == 0x00);
      CHECK(z80.cycle_count() == 12);
    }
    SECTION("not taken") {
      regs.set(RegisterFile::R8::F, Flags().to_u8());
      run_one_instruction(0x28, 0xfe); // jr z -2  ; self
      CHECK(z80.pc() == 0x02);
      CHECK(z80.cycle_count() == 7);
    }
  }
  SECTION("jr nc") {
    SECTION("taken") {
      regs.set(RegisterFile::R8::F, Flags().to_u8());
      run_one_instruction(0x30, 0xfe); // jr nc -2  ; self
      CHECK(z80.pc() == 0x00);
      CHECK(z80.cycle_count() == 12);
    }
    SECTION("not taken") {
      regs.set(RegisterFile::R8::F, Flags::Carry().to_u8());
      run_one_instruction(0x30, 0xfe); // jr nc -2  ; self
      CHECK(z80.pc() == 0x02);
      CHECK(z80.cycle_count() == 7);
    }
  }
  SECTION("jr c") {
    SECTION("taken") {
      regs.set(RegisterFile::R8::F, Flags::Carry().to_u8());
      run_one_instruction(0x38, 0xfe); // jr c -2  ; self
      CHECK(z80.pc() == 0x00);
      CHECK(z80.cycle_count() == 12);
    }
    SECTION("not taken") {
      regs.set(RegisterFile::R8::F, Flags().to_u8());
      run_one_instruction(0x38, 0xfe); // jr c -2  ; self
      CHECK(z80.pc() == 0x02);
      CHECK(z80.cycle_count() == 7);
    }
  }
  SECTION("add hl, ...") {
    regs.set(RegisterFile::R16::HL, 0x1234);
    SECTION("bc") {
      regs.set(RegisterFile::R16::BC, 0x1111);
      run_one_instruction(0x09); // add hl, bc
      CHECK(z80.pc() == 1);
      CHECK(z80.cycle_count() == 11);
      CHECK(regs.get(RegisterFile::R16::HL) == 0x2345);
      CHECK(z80.flags() == Flags());
    }
    SECTION("de") {
      regs.set(RegisterFile::R16::DE, 0x1111);
      run_one_instruction(0x19); // add hl, de
      CHECK(z80.pc() == 1);
      CHECK(z80.cycle_count() == 11);
      CHECK(regs.get(RegisterFile::R16::HL) == 0x2345);
    }
    SECTION("hl") {
      run_one_instruction(0x29); // add hl, hl
      CHECK(z80.pc() == 1);
      CHECK(z80.cycle_count() == 11);
      CHECK(regs.get(RegisterFile::R16::HL) == 0x2468);
    }
    // SECTION("sp") {
    //   regs.sp(0x1111);
    //   run_one_instruction(0x39); // add hl, sp
    //   CHECK(z80.pc() == 1);
    //   CHECK(z80.cycle_count() == 11);
    //   CHECK(regs.get(RegisterFile::R16::HL) == 0x2345);
    // }
  }
}

} // namespace specbolt
