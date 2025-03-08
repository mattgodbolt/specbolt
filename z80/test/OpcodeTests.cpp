
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
      decode_and_run(z80);
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
  SECTION("halt") {
    run_one_instruction(0x76); // halt
    CHECK(z80.halted());
    CHECK(z80.pc() == 0);
    CHECK(z80.cycle_count() == 4);
  }
  SECTION("ld a, h") {
    regs.set(RegisterFile::R8::H, 0x12);
    run_one_instruction(0x7c); // ld a, h
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::A) == 0x12);
  }
  SECTION("ld h, a") {
    regs.set(RegisterFile::R8::A, 0x23);
    run_one_instruction(0x67); // ld h, a
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(regs.get(RegisterFile::R8::H) == 0x23);
  }
  SECTION("ld h, (hl)") {
    regs.set(RegisterFile::R16::HL, 0x1234);
    memory.write(0x1234, 0xc0);
    run_one_instruction(0x66); // ld h, (hl)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(regs.get(RegisterFile::R8::H) == 0xc0);
  }
  SECTION("ld (hl), d") {
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R8::D, 0xfa);
    run_one_instruction(0x72); // ld (hl), d
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(memory.read(0x1234) == 0xfa);
  }
  SECTION("add a, (hl)") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R8::A, 0x55);
    memory.write(0x1234, 0x88);
    run_one_instruction(0x86); // add a, (hl)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(z80.flags() == (Flags::Sign() | Flags::Flag3()));
    CHECK(regs.get(RegisterFile::R8::A) == 0xdd);
  }
  SECTION("add a, d") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::D, 0x88);
    regs.set(RegisterFile::R8::A, 0x55);
    run_one_instruction(0x82); // add a, d
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(z80.flags() == (Flags::Sign() | Flags::Flag3()));
    CHECK(regs.get(RegisterFile::R8::A) == 0xdd);
  }
  SECTION("adc a, (hl)") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R8::A, 0x55);
    memory.write(0x1234, 0x88);
    run_one_instruction(0x8e); // adc a, (hl)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(z80.flags() == (Flags::Sign() | Flags::Flag3()));
    CHECK(regs.get(RegisterFile::R8::A) == 0xde);
  }
  SECTION("adc a, d") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::D, 0x88);
    regs.set(RegisterFile::R8::A, 0x55);
    run_one_instruction(0x8a); // adc a, d
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(z80.flags() == (Flags::Sign() | Flags::Flag3()));
    CHECK(regs.get(RegisterFile::R8::A) == 0xde);
  }

  SECTION("sub a, (hl)") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R8::A, 0x55);
    memory.write(0x1234, 0x88);
    run_one_instruction(0x96); // sub a, (hl)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(z80.flags() == (Flags::Sign() | Flags::HalfCarry() | Flags::Flag3() | Flags::Subtract() | Flags::Overflow() |
                             Flags::Carry()));
    CHECK(regs.get(RegisterFile::R8::A) == 0xcd);
  }
  SECTION("sub a, d") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::D, 0x88);
    regs.set(RegisterFile::R8::A, 0x55);
    run_one_instruction(0x92); // sub a, d
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(z80.flags() == (Flags::Sign() | Flags::HalfCarry() | Flags::Flag3() | Flags::Subtract() | Flags::Overflow() |
                             Flags::Carry()));
    CHECK(regs.get(RegisterFile::R8::A) == 0xcd);
  }
  SECTION("sbc a, (hl)") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R8::A, 0x55);
    memory.write(0x1234, 0x88);
    run_one_instruction(0x9e); // sbc a, (hl)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(z80.flags() == (Flags::Sign() | Flags::HalfCarry() | Flags::Flag3() | Flags::Subtract() | Flags::Overflow() |
                             Flags::Carry()));
    CHECK(regs.get(RegisterFile::R8::A) == 0xcc);
  }
  SECTION("sbc a, d") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::D, 0x88);
    regs.set(RegisterFile::R8::A, 0x55);
    run_one_instruction(0x9a); // sbc a, d
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(z80.flags() == (Flags::Sign() | Flags::HalfCarry() | Flags::Flag3() | Flags::Subtract() | Flags::Overflow() |
                             Flags::Carry()));
    CHECK(regs.get(RegisterFile::R8::A) == 0xcc);
  }
  SECTION("and (hl)") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R8::A, 0x5f);
    memory.write(0x1234, 0x88);
    run_one_instruction(0xa6); // and (hl)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(z80.flags() == (Flags::HalfCarry() | Flags::Flag3()));
    CHECK(regs.get(RegisterFile::R8::A) == (0x5f & 0x88));
  }
  SECTION("or (hl)") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R8::A, 0x5f);
    memory.write(0x1234, 0x88);
    run_one_instruction(0xb6); // or (hl)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(z80.flags() == (Flags::Sign() | Flags::Flag3()));
    CHECK(regs.get(RegisterFile::R8::A) == (0x5f | 0x88));
  }
  SECTION("xor (hl)") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R8::A, 0x5f);
    memory.write(0x1234, 0x88);
    run_one_instruction(0xae); // xor (hl)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(z80.flags() == (Flags::Parity() | Flags::Sign()));
    CHECK(regs.get(RegisterFile::R8::A) == (0x5f ^ 0x88));
  }
  SECTION("cp (hl)") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R8::A, 0x5f);
    memory.write(0x1234, 0x88);
    run_one_instruction(0xbe); // cp (hl)
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 7);
    CHECK(z80.flags() ==
          (Flags::Parity() | Flags::Carry() | Flags::Sign() | Flags::Flag3() | Flags::Overflow() | Flags::Subtract()));
    CHECK(regs.get(RegisterFile::R8::A) == 0x5f);
  }
  SECTION("return") {
    regs.sp(0xfffd);
    memory.write16(0xfffd, 0xcafe);
    SECTION("ret") {
      run_one_instruction(0xc9); // ret
      CHECK(z80.pc() == 0xcafe);
      CHECK(z80.cycle_count() == 10);
    }
    SECTION("ret nz") {
      SECTION("taken") {
        z80.flags(Flags());
        run_one_instruction(0xc0); // ret nz
        CHECK(z80.pc() == 0xcafe);
        CHECK(z80.cycle_count() == 11);
      }
      SECTION("not taken") {
        z80.flags(Flags::Zero());
        run_one_instruction(0xc0); // ret nz
        CHECK(z80.pc() == 1);
        CHECK(z80.cycle_count() == 5);
      }
    }
    SECTION("ret z") {
      SECTION("taken") {
        z80.flags(Flags::Zero());
        run_one_instruction(0xc8); // ret z
        CHECK(z80.pc() == 0xcafe);
        CHECK(z80.cycle_count() == 11);
      }
      SECTION("not taken") {
        z80.flags(Flags());
        run_one_instruction(0xc8); // ret z
        CHECK(z80.pc() == 1);
        CHECK(z80.cycle_count() == 5);
      }
    }
    // todo ... consider testing the others though honestly...
  }
  SECTION("call") {
    regs.sp(0x0000);
    SECTION("call") {
      run_one_instruction(0xcd, 0x34, 0x12); // call 0x1234
      CHECK(z80.pc() == 0x1234);
      CHECK(z80.cycle_count() == 17);
      CHECK(memory.read16(0xfffe) == 0x0003);
      CHECK(regs.sp() == 0xfffe);
    }
    SECTION("call nc") {
      SECTION("taken") {
        z80.flags(Flags());
        run_one_instruction(0xd4, 0x34, 0x12); // call nc 0x1234
        CHECK(z80.pc() == 0x1234);
        CHECK(z80.cycle_count() == 17);
        CHECK(memory.read16(0xfffe) == 0x0003);
        CHECK(regs.sp() == 0xfffe);
      }
      SECTION("not taken") {
        z80.flags(Flags::Carry());
        run_one_instruction(0xd4, 0x34, 0x12); // call nc 0x1234
        CHECK(z80.pc() == 3);
        CHECK(z80.cycle_count() == 10);
        CHECK(regs.sp() == 0);
      }
    }
    SECTION("call c") {
      SECTION("taken") {
        z80.flags(Flags::Carry());
        run_one_instruction(0xdc, 0x34, 0x12); // call c 0x1234
        CHECK(z80.pc() == 0x1234);
        CHECK(z80.cycle_count() == 17);
        CHECK(memory.read16(0xfffe) == 0x0003);
        CHECK(regs.sp() == 0xfffe);
      }
      SECTION("not taken") {
        z80.flags(Flags());
        run_one_instruction(0xdc, 0x34, 0x12); // call c 0x1234
        CHECK(z80.pc() == 3);
        CHECK(z80.cycle_count() == 10);
        CHECK(regs.sp() == 0);
      }
    }
    // todo ... consider testing the others though honestly...
  }
  SECTION("pop bc") {
    regs.sp(0xfffd);
    memory.write16(0xfffd, 0xbab1);
    run_one_instruction(0xc1); // pop bc
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 10);
    CHECK(z80.regs().get(RegisterFile::R16::BC) == 0xbab1);
    CHECK(z80.regs().get(RegisterFile::R16::SP) == 0xffff);
  }
  SECTION("pop af") {
    regs.sp(0xfffd);
    memory.write16(0xfffd, 0xbab1);
    run_one_instruction(0xf1); // pop af
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 10);
    CHECK(z80.regs().get(RegisterFile::R16::AF) == 0xbab1);
    CHECK(z80.regs().get(RegisterFile::R16::SP) == 0xffff);
  }
  SECTION("exx") {
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R16::DE, 0x2345);
    regs.set(RegisterFile::R16::BC, 0x3456);
    regs.set(RegisterFile::R16::HL_, 0xfeed);
    regs.set(RegisterFile::R16::DE_, 0xbeef);
    regs.set(RegisterFile::R16::BC_, 0xdead);
    run_one_instruction(0xd9); // exx
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(z80.regs().get(RegisterFile::R16::HL) == 0xfeed);
    CHECK(z80.regs().get(RegisterFile::R16::DE) == 0xbeef);
    CHECK(z80.regs().get(RegisterFile::R16::BC) == 0xdead);
    CHECK(z80.regs().get(RegisterFile::R16::HL_) == 0x1234);
    CHECK(z80.regs().get(RegisterFile::R16::DE_) == 0x2345);
    CHECK(z80.regs().get(RegisterFile::R16::BC_) == 0x3456);
  }
  SECTION("jp (hl)") {
    regs.set(RegisterFile::R16::HL, 0x8008);
    run_one_instruction(0xe9); // jp (hl)
    CHECK(z80.pc() == 0x8008);
    CHECK(z80.cycle_count() == 4);
  }
  SECTION("ld sp, hl") {
    regs.set(RegisterFile::R16::HL, 0x8008);
    run_one_instruction(0xf9); // ld sp, hl
    CHECK(z80.pc() == 1);
    CHECK(z80.regs().sp() == 0x8008);
    CHECK(z80.cycle_count() == 6);
  }
  SECTION("jump") {
    SECTION("unconditional") {
      run_one_instruction(0xc3, 0xfe, 0xca); // jp 0xcafe
      CHECK(z80.pc() == 0xcafe);
      CHECK(z80.cycle_count() == 10);
    }
    SECTION("jp nz") {
      SECTION("taken") {
        z80.flags(Flags());
        run_one_instruction(0xc2, 0xfe, 0xca); // jp nz, 0xcafe
        CHECK(z80.pc() == 0xcafe);
        CHECK(z80.cycle_count() == 10);
      }
      SECTION("not taken") {
        z80.flags(Flags::Zero());
        run_one_instruction(0xc2, 0xfe, 0xca); // jp nz, 0xcafe
        CHECK(z80.pc() == 3);
        CHECK(z80.cycle_count() == 10);
      }
    }
    SECTION("jp z") {
      SECTION("taken") {
        z80.flags(Flags::Zero());
        run_one_instruction(0xca, 0xfe, 0xca); // jp z, 0xcafe
        CHECK(z80.pc() == 0xcafe);
        CHECK(z80.cycle_count() == 10);
      }
      SECTION("not taken") {
        z80.flags(Flags());
        run_one_instruction(0xca, 0xfe, 0xca); // jp z, 0xcafe
        CHECK(z80.pc() == 3);
        CHECK(z80.cycle_count() == 10);
      }
    }
    // todo ... consider testing the others though honestly...
  }
  SECTION("out ($nn), a") {
    std::vector<std::pair<std::uint16_t, std::uint8_t>> outs;
    z80.add_out_handler(
        [&](const std::uint16_t port, const std::uint8_t value) { outs.emplace_back(std::make_pair(port, value)); });
    z80.regs().set(RegisterFile::R8::A, 0xbe);
    run_one_instruction(0xd3, 0x12); // out (0x12), a
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 11);
    CHECK(outs == std::vector{std::make_pair<std::uint16_t, std::uint8_t>(0xbe12, 0xbe)});
  }
  SECTION("in a, ($nn)") {
    z80.flags(Flags());
    std::vector<std::uint16_t> ins;
    z80.add_in_handler([&](const std::uint16_t port) {
      ins.emplace_back(port);
      return 0xac;
    });
    z80.regs().set(RegisterFile::R8::A, 0xbe);
    run_one_instruction(0xdb, 0x21); // in a, (0x21)
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 11);
    CHECK(z80.regs().get(RegisterFile::R8::A) == 0xac);
    CHECK(z80.flags() == Flags());
    CHECK(ins == std::vector<std::uint16_t>{0xbe21});
  }
  SECTION("ex (sp), hl") {
    regs.sp(0xfffd);
    memory.write16(0xfffd, 0xbabe);
    regs.set(RegisterFile::R16::HL, 0xcafe);
    run_one_instruction(0xe3); // ex (sp), hl
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 19);
    CHECK(z80.regs().get(RegisterFile::R16::HL) == 0xbabe);
    CHECK(memory.read16(0xfffd) == 0xcafe);
  }
  SECTION("ex de, hl") {
    regs.set(RegisterFile::R16::DE, 0x0102);
    regs.set(RegisterFile::R16::HL, 0xcafe);
    run_one_instruction(0xeb); // ex de, hl
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(z80.regs().get(RegisterFile::R16::HL) == 0x0102);
    CHECK(z80.regs().get(RegisterFile::R16::DE) == 0xcafe);
  }
  SECTION("di") {
    z80.iff1(false);
    z80.iff2(true);
    run_one_instruction(0xf3); // di
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(!z80.iff1());
    CHECK(!z80.iff2());
  }
  SECTION("ei") {
    z80.iff1(false);
    z80.iff2(true);
    run_one_instruction(0xfb); // ei
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 4);
    CHECK(z80.iff1());
    CHECK(z80.iff2());
  }
  SECTION("push bc") {
    regs.sp(0xfffd);
    regs.set(RegisterFile::R16::BC, 0xabcd);
    run_one_instruction(0xc5); // push bc
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 11);
    CHECK(memory.read16(0xfffb) == 0xabcd);
    CHECK(regs.sp() == 0xfffb);
  }
  SECTION("push af") {
    regs.sp(0xfffd);
    regs.set(RegisterFile::R16::AF, 0xabcd);
    run_one_instruction(0xf5); // push af
    CHECK(z80.pc() == 1);
    CHECK(z80.cycle_count() == 11);
    CHECK(memory.read16(0xfffb) == 0xabcd);
    CHECK(regs.sp() == 0xfffb);
  }
  SECTION("add $nn") {
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::A, 0x5f);
    run_one_instruction(0xc6, 0x87); // add 0x87
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 7);
    CHECK(z80.flags() == (Flags::Sign() | Flags::Flag5() | Flags::HalfCarry()));
    CHECK(regs.get(RegisterFile::R8::A) == 0x5f + 0x87);
    // assumed the other ALU ops work if this one does...
  }
  SECTION("rst 0x20") {
    regs.sp(0x0000);
    run_one_instruction(0xe7); // rst 20
    CHECK(z80.pc() == 0x20);
    CHECK(z80.cycle_count() == 11);
    CHECK(memory.read16(0xfffe) == 0x0001);
    CHECK(regs.sp() == 0xfffe);
  }
  SECTION("shifts & rotates") {
    z80.flags(Flags());
    regs.set(RegisterFile::R8::A, 0b1000'0001);
    regs.set(RegisterFile::R16::HL, 0x1234);
    memory.write(0x1234, 0b1100'0011);
    SECTION("rlc a") {
      run_one_instruction(0xcb, 0x07); // rlc a
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 8);
      CHECK(regs.get(RegisterFile::R8::A) == 0b0000'0011);
      CHECK(z80.flags() == (Flags::Parity() | Flags::Carry()));
    }
    SECTION("rlc (hl)") {
      run_one_instruction(0xcb, 0x06); // rlc (hl)
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 15);
      CHECK(memory.read(0x1234) == 0b1000'0111);
      CHECK(z80.flags() == (Flags::Sign() | Flags::Parity() | Flags::Carry()));
    }
    SECTION("rrc a") {
      run_one_instruction(0xcb, 0x0f); // rrc a
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 8);
      CHECK(regs.get(RegisterFile::R8::A) == 0b1100'0000);
      CHECK(z80.flags() == (Flags::Sign() | Flags::Parity() | Flags::Carry()));
    }
    SECTION("rrc (hl)") {
      run_one_instruction(0xcb, 0x0e); // rrc (hl)
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 15);
      CHECK(memory.read(0x1234) == 0b1110'0001);
      CHECK(z80.flags() == (Flags::Sign() | Flags::Parity() | Flags::Flag5() | Flags::Carry()));
    }
    SECTION("rl a") {
      run_one_instruction(0xcb, 0x17); // rl a
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 8);
      CHECK(regs.get(RegisterFile::R8::A) == 0b0000'0010);
      CHECK(z80.flags() == Flags::Carry());
    }
    SECTION("rl (hl)") {
      run_one_instruction(0xcb, 0x16); // rl (hl)
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 15);
      CHECK(memory.read(0x1234) == 0b1000'0110);
      CHECK(z80.flags() == (Flags::Sign() | Flags::Carry()));
    }
    SECTION("rr a") {
      run_one_instruction(0xcb, 0x1f); // rr a
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 8);
      CHECK(regs.get(RegisterFile::R8::A) == 0b0100'0000);
      CHECK(z80.flags() == Flags::Carry());
    }
    SECTION("rr (hl)") {
      run_one_instruction(0xcb, 0x1e); // rr (hl)
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 15);
      CHECK(memory.read(0x1234) == 0b0110'0001);
      CHECK(z80.flags() == (Flags::Flag5() | Flags::Carry()));
    }
    SECTION("sla a") {
      run_one_instruction(0xcb, 0x27); // sla a
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 8);
      CHECK(regs.get(RegisterFile::R8::A) == 0b0000'0010);
      CHECK(z80.flags() == Flags::Carry());
    }
    SECTION("sla (hl)") {
      run_one_instruction(0xcb, 0x26); // sla (hl)
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 15);
      CHECK(memory.read(0x1234) == 0b1000'0110);
      CHECK(z80.flags() == (Flags::Sign() | Flags::Carry()));
    }
    SECTION("sra a") {
      run_one_instruction(0xcb, 0x2f); // sra a
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 8);
      CHECK(regs.get(RegisterFile::R8::A) == 0b1100'0000);
      CHECK(z80.flags() == (Flags::Parity() | Flags::Sign() | Flags::Carry()));
    }
    SECTION("sra (hl)") {
      run_one_instruction(0xcb, 0x2e); // sra (hl)
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 15);
      CHECK(memory.read(0x1234) == 0b1110'0001);
      CHECK(z80.flags() == (Flags::Sign() | Flags::Parity() | Flags::Flag5() | Flags::Carry()));
    }
    SECTION("sll a") {
      run_one_instruction(0xcb, 0x37); // sll a
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 8);
      CHECK(regs.get(RegisterFile::R8::A) == 0b0000'0011);
      CHECK(z80.flags() == (Flags::Parity() | Flags::Carry()));
    }
    SECTION("sll (hl)") {
      run_one_instruction(0xcb, 0x36); // sll (hl)
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 15);
      CHECK(memory.read(0x1234) == 0b1000'0111);
      CHECK(z80.flags() == (Flags::Parity() | Flags::Sign() | Flags::Carry()));
    }
    SECTION("srl a") {
      run_one_instruction(0xcb, 0x3f); // srl a
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 8);
      CHECK(regs.get(RegisterFile::R8::A) == 0b0100'0000);
      CHECK(z80.flags() == Flags::Carry());
    }
    SECTION("srl (hl)") {
      run_one_instruction(0xcb, 0x3e); // srl (hl)
      CHECK(z80.pc() == 2);
      CHECK(z80.cycle_count() == 15);
      CHECK(memory.read(0x1234) == 0b0110'0001);
      CHECK(z80.flags() == (Flags::Flag5() | Flags::Carry()));
    }
  }
  SECTION("bit 0, b") {
    z80.flags(Flags());
    regs.set(RegisterFile::R8::B, 0x00);
    run_one_instruction(0xcb, 0x40); // bit 0, b
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 8);
    CHECK(z80.flags() == (Flags::Zero() | Flags::HalfCarry() | Flags::Parity()));
    regs.set(RegisterFile::R8::B, 0x01);
    run_one_instruction(0xcb, 0x40); // bit 0, b
    CHECK(z80.flags() == Flags::HalfCarry());
    regs.set(RegisterFile::R8::B, 0xff);
    run_one_instruction(0xcb, 0x40); // bit 0, b
    CHECK(z80.flags() == (Flags::HalfCarry() | Flags::Flag3() | Flags::Flag5()));
  }
  SECTION("bit 0, (hl)") {
    z80.flags(Flags());
    regs.set(RegisterFile::R16::HL, 0x1234);
    memory.write(0x1234, 0x00);
    regs.set(RegisterFile::R8::B, 0x00);
    run_one_instruction(0xcb, 0x46); // bit 0, (hl)
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 12);
    CHECK(z80.flags() == (Flags::Zero() | Flags::HalfCarry() | Flags::Parity()));
    memory.write(0x1234, 0x01);
    run_one_instruction(0xcb, 0x46); // bit 0, (hl)
    CHECK(z80.flags() == Flags::HalfCarry());
    memory.write(0x1234, 0xff);
    run_one_instruction(0xcb, 0x46); // bit 0, (hl)
    CHECK(z80.flags() == Flags::HalfCarry());
    // Test behaviour with wz bits that come from the top bits of the address of HL.
    regs.set(RegisterFile::R16::HL, 0xff34);
    memory.write(0xff34, 0xff);
    run_one_instruction(0xcb, 0x46); // bit 0, (hl)
    CHECK(z80.flags() == (Flags::HalfCarry() | Flags::Flag3() | Flags::Flag5()));
  }
  SECTION("res 4, e") {
    regs.set(RegisterFile::R8::E, 0x7f);
    run_one_instruction(0xcb, 0xa3); // res 4, e
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 8);
    CHECK(regs.get(RegisterFile::R8::E) == (0x7f & ~(1 << 4)));
  }
  SECTION("res 4, (hl)") {
    regs.set(RegisterFile::R16::HL, 0x1234);
    memory.write(0x1234, 0x7f);
    run_one_instruction(0xcb, 0xa6); // res 4, e
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 15);
    CHECK(memory.read(0x1234) == (0x7f & ~(1 << 4)));
  }
  SECTION("set 7, a") {
    regs.set(RegisterFile::R8::A, 0x12);
    run_one_instruction(0xcb, 0xff); // set 7, a
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 8);
    CHECK(regs.get(RegisterFile::R8::A) == (0x12 | (1 << 7)));
  }
  SECTION("set 7, (hl)") {
    regs.set(RegisterFile::R16::HL, 0x1234);
    memory.write(0x1234, 0x12);
    run_one_instruction(0xcb, 0xfe); // set 7, (hl)
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 15);
    CHECK(memory.read(0x1234) == (0x12 | (1 << 7)));
  }
}

} // namespace specbolt
