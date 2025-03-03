
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
    regs.set(RegisterFile::R8::F, 0);
    SECTION("bc") {
      regs.set(RegisterFile::R16::BC, 0x1111);
      run_one_instruction(0x09); // add hl, bc
      CHECK(z80.pc() == 1);
      CHECK(z80.cycle_count() == 11);
      CHECK(regs.get(RegisterFile::R16::HL) == 0x2345);
      CHECK(z80.flags() == Flags::Flag5());
    }
    SECTION("de") {
      regs.set(RegisterFile::R16::DE, 0x1111);
      run_one_instruction(0x19); // add hl, de
      CHECK(z80.pc() == 1);
      CHECK(z80.cycle_count() == 11);
      CHECK(regs.get(RegisterFile::R16::HL) == 0x2345);
      CHECK(z80.flags() == Flags::Flag5());
    }
    SECTION("hl") {
      run_one_instruction(0x29); // add hl, hl
      CHECK(z80.pc() == 1);
      CHECK(z80.cycle_count() == 11);
      CHECK(regs.get(RegisterFile::R16::HL) == 0x2468);
      CHECK(z80.flags() == Flags::Flag5());
    }
    SECTION("sp") {
      regs.sp(0x1111);
      run_one_instruction(0x39); // add hl, sp
      CHECK(z80.pc() == 1);
      CHECK(z80.cycle_count() == 11);
      CHECK(regs.get(RegisterFile::R16::HL) == 0x2345);
      CHECK(z80.flags() == Flags::Flag5());
    }
  }
  SECTION("ld a, (bc)") {
    regs.set(RegisterFile::R16::BC, 0x1234);
    memory.write(0x1234, 0xc2);
    run_one_instruction(0x0a); // ld a, (bc)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(regs.get(RegisterFile::R8::A) == 0xc2);
  }
  SECTION("ld (bc), a") {
    regs.set(RegisterFile::R8::A, 0xf0);
    regs.set(RegisterFile::R16::BC, 0x1234);
    run_one_instruction(0x02); // ld (bc), a
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(memory.read(0x1234) == 0xf0);
  }
  SECTION("ld a, (de)") {
    regs.set(RegisterFile::R16::DE, 0x1234);
    memory.write(0x1234, 0xc2);
    run_one_instruction(0x1a); // ld a, (de)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(regs.get(RegisterFile::R8::A) == 0xc2);
  }
  SECTION("ld (de), a") {
    regs.set(RegisterFile::R8::A, 0xf0);
    regs.set(RegisterFile::R16::DE, 0x1234);
    run_one_instruction(0x12); // ld (de), a
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(memory.read(0x1234) == 0xf0);
  }
  SECTION("ld ($nnnn), hl") {
    regs.set(RegisterFile::R16::HL, 0xf00f);
    run_one_instruction(0x22, 0x34, 0x12); // ld (0x1234), hl
    CHECK(z80.pc() == 3);
    CHECK(z80.cycle_count() == 16);
    CHECK(memory.read16(0x1234) == 0xf00f);
  }
  SECTION("ld ($nnnn), a") {
    regs.set(RegisterFile::R16::HL, 0xf00f);
    regs.set(RegisterFile::R8::A, 0xc7);
    run_one_instruction(0x32, 0x34, 0x12); // ld (0x1234), a
    CHECK(z80.pc() == 3);
    CHECK(z80.cycle_count() == 13);
    CHECK(memory.read(0x1234) == 0xc7);
  }
  SECTION("ld hl, ($nnnn)") {
    memory.write16(0x1234, 0xf11f);
    run_one_instruction(0x2a, 0x34, 0x12); // ld hl, ($nnnn)
    CHECK(z80.pc() == 3);
    CHECK(z80.cycle_count() == 16);
    CHECK(regs.get(RegisterFile::R16::HL) == 0xf11f);
  }
  SECTION("ld a, ($nnnn)") {
    memory.write(0x1234, 0xc2);
    run_one_instruction(0x3a, 0x34, 0x12); // ld a, ($nnnn)
    CHECK(z80.pc() == 3);
    CHECK(z80.cycle_count() == 13);
    CHECK(regs.get(RegisterFile::R8::A) == 0xc2);
  }
  SECTION("inc hl") {
    regs.set(RegisterFile::R16::HL, 0xffff);
    regs.set(RegisterFile::R8::F, 0);
    run_one_instruction(0x23); // inc hl
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 6);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x0000);
    CHECK(z80.flags() == Flags());
  }
  SECTION("dec hl") {
    regs.set(RegisterFile::R16::HL, 0x0000);
    regs.set(RegisterFile::R8::F, 0);
    run_one_instruction(0x2b); // dec hl
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 6);
    CHECK(regs.get(RegisterFile::R16::HL) == 0xffff);
    CHECK(z80.flags() == Flags());
  }
  SECTION("inc h") {
    regs.set(RegisterFile::R8::F, 0);
    regs.set(RegisterFile::R8::H, 0xff);
    run_one_instruction(0x24); // inc h
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::H) == 0x00);
    CHECK(z80.flags() == (Flags::Zero() | Flags::HalfCarry()));
  }
  SECTION("inc a") {
    regs.set(RegisterFile::R8::F, 0);
    regs.set(RegisterFile::R8::A, 0xff);
    run_one_instruction(0x3c); // inc a
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::A) == 0x00);
    CHECK(z80.flags() == (Flags::Zero() | Flags::HalfCarry()));
  }
  SECTION("inc (hl)") {
    regs.set(RegisterFile::R8::F, 0);
    regs.set(RegisterFile::R16::HL, 0x1234);
    memory.write(0x1234, 0xff);
    run_one_instruction(0x34);
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 11);
    CHECK(memory.read(0x1234) == 0);
    CHECK(z80.flags() == (Flags::Zero() | Flags::HalfCarry()));
  }
  SECTION("dec h") {
    regs.set(RegisterFile::R8::F, 0);
    regs.set(RegisterFile::R8::H, 0x00);
    run_one_instruction(0x25); // dec h
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::H) == 0xff);
    CHECK(z80.flags() == (Flags::Sign() | Flags::Flag5() | Flags::HalfCarry() | Flags::Flag3() | Flags::Subtract()));
  }
  SECTION("dec a") {
    regs.set(RegisterFile::R8::F, 0);
    regs.set(RegisterFile::R8::A, 0x00);
    run_one_instruction(0x3d); // dec a
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::A) == 0xff);
    CHECK(z80.flags() == (Flags::Sign() | Flags::Flag5() | Flags::HalfCarry() | Flags::Flag3() | Flags::Subtract()));
  }
  SECTION("dec (hl)") {
    regs.set(RegisterFile::R8::F, 0);
    regs.set(RegisterFile::R16::HL, 0x1234);
    memory.write(0x1234, 0x00);
    run_one_instruction(0x35);
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 11);
    CHECK(memory.read(0x1234) == 0xff);
    CHECK(z80.flags() == (Flags::Sign() | Flags::Flag5() | Flags::HalfCarry() | Flags::Flag3() | Flags::Subtract()));
  }
  SECTION("ld h, $nn") {
    run_one_instruction(0x26, 0xde); // ld hd, 0xde
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 7);
    CHECK(regs.get(RegisterFile::R8::H) == 0xde);
  }
  SECTION("ld (hl), $nn") {
    regs.set(RegisterFile::R16::HL, 0x1234);
    run_one_instruction(0x36, 0xba); // ld (hl), 0xba
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 10);
    CHECK(memory.read(0x1234) == 0xba);
  }
  SECTION("rlca") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::A, 0x59);
    run_one_instruction(0x07); // rlca
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::A) == 0xb2);
    CHECK(z80.flags() == Flags::Flag5());
  }
  SECTION("rrca") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::A, 0x59);
    run_one_instruction(0x0f); // rrca
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::A) == 0xac);
    CHECK(z80.flags() == (Flags::Flag5() | Flags::Flag3() | Flags::Carry()));
  }
  SECTION("rla") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::A, 0x59);
    run_one_instruction(0x17); // rla
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::A) == 0xb3);
    CHECK(z80.flags() == Flags::Flag5());
  }
  SECTION("rra") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::A, 0x59);
    run_one_instruction(0x1f); // rra
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::A) == 0xac);
    CHECK(z80.flags() == (Flags::Flag5() | Flags::Flag3() | Flags::Carry()));
  }
  SECTION("daa") {
    z80.flags(Flags::HalfCarry());
    regs.set(RegisterFile::R8::A, 0x59);
    run_one_instruction(0x27); // daa
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::A) == 0x5f);
    CHECK(z80.flags() == (Flags::Flag3() | Flags::Parity()));
  }
  SECTION("cpl") {
    z80.flags(Flags::HalfCarry());
    regs.set(RegisterFile::R8::A, 0x59);
    run_one_instruction(0x2f); // cpl
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::A) == 0xa6);
    CHECK(z80.flags() == (Flags::Flag5() | Flags::HalfCarry() | Flags::Subtract()));
  }
  SECTION("scf") {
    z80.flags(Flags::HalfCarry());
    regs.set(RegisterFile::R8::A, 0x59);
    run_one_instruction(0x37); // scf
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(z80.flags() == (Flags::Flag3() | Flags::Carry()));
    CHECK(regs.get(RegisterFile::R8::A) == 0x59);
  }
  SECTION("ccf") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::A, 0x59);
    run_one_instruction(0x3f); // ccf
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(z80.flags() == (Flags::Flag3() | Flags::HalfCarry()));
    CHECK(regs.get(RegisterFile::R8::A) == 0x59);
  }
}

} // namespace specbolt
