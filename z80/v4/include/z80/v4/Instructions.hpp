#pragma once

// The v4 instruction set lives as data: one entry per opcode, with the
// behaviour expressed as a stored token sequence. At class-injection time
// the dispatch function is synthesised by splicing each body into a switch
// case. This is the floooh-style "table is the source of truth" design,
// expressed inside C++ rather than in a separate codegen tool.
//
// This file holds the *one-off* opcodes that don't fit a parameterised
// shape. The parametric ones live in Shapes.hpp.

#include "z80/common/Alu.hpp"

#include <array>
#include <cstdint>
#include <meta>
#include <string_view>

namespace specbolt::v4 {

struct InsnDef {
  std::uint8_t opcode;
  std::string_view mnemonic;
  std::meta::token_sequence body;
};

inline constexpr auto base_instructions = std::array{
    // ---- 0x00 group -----------------------------------------------------
    InsnDef{0x00, "nop", ^^{}},
    InsnDef{0x08, "ex af, af'", ^^{ regs().ex(RegisterFile::R16::AF, RegisterFile::R16::AF_); }},
    InsnDef{0x10, "djnz $d", ^^{
      pass_time(1);
      const auto offset = static_cast<std::int8_t>(read_immediate());
      const std::uint8_t new_b = static_cast<std::uint8_t>(regs().get(RegisterFile::R8::B) - 1);
      regs().set(RegisterFile::R8::B, new_b);
      if (new_b != 0) {
        pass_time(5);
        regs().pc(static_cast<std::uint16_t>(regs().pc() + offset));
      }
    }},
    InsnDef{0x18, "jr $d", ^^{
      const auto offset = static_cast<std::int8_t>(read_immediate());
      pass_time(5);
      regs().pc(static_cast<std::uint16_t>(regs().pc() + offset));
    }},

    // Fixed-source/dest 8-bit and 16-bit loads through (BC)/(DE)/($nnnn).
    InsnDef{0x02, "ld (bc), a", ^^{
      write(regs().get(RegisterFile::R16::BC), regs().get(RegisterFile::R8::A));
    }},
    InsnDef{0x12, "ld (de), a", ^^{
      write(regs().get(RegisterFile::R16::DE), regs().get(RegisterFile::R8::A));
    }},
    InsnDef{0x22, "ld ($nnnn), hl", ^^{
      const auto addr = read_immediate16();
      write(addr,
            regs().get(RegisterFile::R8::L));
      write(static_cast<std::uint16_t>(addr + 1),
            regs().get(RegisterFile::R8::H));
    }},
    InsnDef{0x32, "ld ($nnnn), a", ^^{
      write(read_immediate16(), regs().get(RegisterFile::R8::A));
    }},
    InsnDef{0x0a, "ld a, (bc)", ^^{
      regs().set(RegisterFile::R8::A, read(regs().get(RegisterFile::R16::BC)));
    }},
    InsnDef{0x1a, "ld a, (de)", ^^{
      regs().set(RegisterFile::R8::A, read(regs().get(RegisterFile::R16::DE)));
    }},
    InsnDef{0x2a, "ld hl, ($nnnn)", ^^{
      const auto addr = read_immediate16();
      regs().set(RegisterFile::R8::L, read(addr));
      regs().set(RegisterFile::R8::H, read(static_cast<std::uint16_t>(addr + 1)));
    }},
    InsnDef{0x3a, "ld a, ($nnnn)", ^^{
      regs().set(RegisterFile::R8::A, read(read_immediate16()));
    }},

    // Accumulator/flag operations on A (0x07/0x0f/0x17/0x1f/0x27/0x2f/0x37/0x3f).
    InsnDef{0x07, "rlca", ^^{
      const auto [result, new_flags] = Alu::fast_rotate_circular8(
          regs().get(RegisterFile::R8::A), Alu::Direction::Left, flags());
      regs().set(RegisterFile::R8::A, result);
      flags(new_flags);
    }},
    InsnDef{0x0f, "rrca", ^^{
      const auto [result, new_flags] = Alu::fast_rotate_circular8(
          regs().get(RegisterFile::R8::A), Alu::Direction::Right, flags());
      regs().set(RegisterFile::R8::A, result);
      flags(new_flags);
    }},
    InsnDef{0x17, "rla", ^^{
      const auto [result, new_flags] = Alu::fast_rotate8(
          regs().get(RegisterFile::R8::A), Alu::Direction::Left, flags());
      regs().set(RegisterFile::R8::A, result);
      flags(new_flags);
    }},
    InsnDef{0x1f, "rra", ^^{
      const auto [result, new_flags] = Alu::fast_rotate8(
          regs().get(RegisterFile::R8::A), Alu::Direction::Right, flags());
      regs().set(RegisterFile::R8::A, result);
      flags(new_flags);
    }},
    InsnDef{0x27, "daa", ^^{
      const auto [result, new_flags] = Alu::daa(regs().get(RegisterFile::R8::A), flags());
      regs().set(RegisterFile::R8::A, result);
      flags(new_flags);
    }},
    InsnDef{0x2f, "cpl", ^^{
      const auto [result, new_flags] = Alu::cpl(regs().get(RegisterFile::R8::A), flags());
      regs().set(RegisterFile::R8::A, result);
      flags(new_flags);
    }},
    InsnDef{0x37, "scf", ^^{
      const auto [result, new_flags] = Alu::scf(regs().get(RegisterFile::R8::A), flags());
      regs().set(RegisterFile::R8::A, result);
      flags(new_flags);
    }},
    InsnDef{0x3f, "ccf", ^^{
      const auto [result, new_flags] = Alu::ccf(regs().get(RegisterFile::R8::A), flags());
      regs().set(RegisterFile::R8::A, result);
      flags(new_flags);
    }},

    // ---- 0x76 HALT, sits in the LD r,r' block ---------------------------
    InsnDef{0x76, "halt", ^^{ halt(); }},

    // ---- 0xc0+ block — odd one-offs that aren't in a shape --------------
    InsnDef{0xc3, "jp $nnnn", ^^{ regs().pc(read_immediate16()); }},
    InsnDef{0xc9, "ret", ^^{ regs().pc(pop16()); }},
    InsnDef{0xcd, "call $nnnn", ^^{
      const auto target = read_immediate16();
      pass_time(1);
      push16(regs().pc());
      regs().pc(target);
    }},
    InsnDef{0xd9, "exx", ^^{ regs().exx(); }},
    InsnDef{0xe3, "ex (sp), hl", ^^{
      const auto sp = regs().sp();
      const auto lo = read(sp);
      pass_time(1);
      const auto hi = read(static_cast<std::uint16_t>(sp + 1));
      write(sp, regs().get(RegisterFile::R8::L));
      pass_time(2);
      write(static_cast<std::uint16_t>(sp + 1), regs().get(RegisterFile::R8::H));
      regs().set(RegisterFile::R16::HL, static_cast<std::uint16_t>(lo | (hi << 8)));
    }},
    InsnDef{0xe9, "jp (hl)", ^^{ regs().pc(regs().get(RegisterFile::R16::HL)); }},
    InsnDef{0xeb, "ex de, hl", ^^{
      regs().ex(RegisterFile::R16::DE, RegisterFile::R16::HL);
    }},
    InsnDef{0xf3, "di", ^^{ iff1(false); iff2(false); }},
    InsnDef{0xf9, "ld sp, hl", ^^{
      pass_time(2);
      regs().sp(regs().get(RegisterFile::R16::HL));
    }},
    InsnDef{0xfb, "ei", ^^{ iff1(true);  iff2(true); }},
};

} // namespace specbolt::v4
