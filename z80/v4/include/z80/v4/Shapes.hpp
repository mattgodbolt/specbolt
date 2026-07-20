#pragma once

// Parameterised instruction shapes. Each "shape" is a consteval function
// that walks a small data-driven pattern and adds N case arms to the
// dispatcher's switch via the supplied list_builder.
//
// RegSet captures everything that varies under DD/FD prefix re-encoding,
// so a single shape generator populates the base, dd, and fd dispatchers.
//
// TODO: derive RegSet's r8_ids array from enumerators_of(^^R8) so the
// hand-written {"B","C","D","E",...} restatement of the enum disappears.
// That's the natural "stock-P2996 reflection" slide.

#include "z80/common/Alu.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/v4/Instructions.hpp"

#include <array>
#include <cstdint>
#include <meta>
#include <string_view>

namespace specbolt::v4 {

struct RegSet {
  // R8 enumerator names for the r-field encoding 0..7 (slot 6 is unused;
  // the indirect form is handled via `indirect_addr` instead).
  std::array<std::string_view, 8> r8_ids;
  std::array<std::string_view, 8> r8_disp;
  std::meta::token_sequence indirect_addr;
  std::string_view indirect_disp;

  // 16-bit register pair names for the p-field (0..3).
  // Slot 2 is the "index register" — HL by default, IX/IY under prefix.
  std::array<std::string_view, 4> rp_ids;  // BC, DE, HL/IX/IY, SP
  std::array<std::string_view, 4> rp2_ids; // BC, DE, HL/IX/IY, AF

  // Standalone name of the index 16-bit register.
  std::string_view index16; // "HL" / "IX" / "IY"
};

inline constexpr RegSet hl_regs{
    {"B", "C", "D", "E", "H", "L", "", "A"},
    {"b", "c", "d", "e", "h", "l", "(hl)", "a"},
    ^^{ regs().get(RegisterFile::R16::HL) },
    "(hl)",
    {"BC", "DE", "HL", "SP"},
    {"BC", "DE", "HL", "AF"},
    "HL",
};

inline constexpr RegSet ix_regs{
    {"B", "C", "D", "E", "IXH", "IXL", "", "A"},
    {"b", "c", "d", "e", "ixh", "ixl", "(ix+d)", "a"},
    ^^{ static_cast<std::uint16_t>(regs().get(RegisterFile::R16::IX)
                                   + static_cast<std::int8_t>(read_immediate())) },
    "(ix+d)",
    {"BC", "DE", "IX", "SP"},
    {"BC", "DE", "IX", "AF"},
    "IX",
};

inline constexpr RegSet iy_regs{
    {"B", "C", "D", "E", "IYH", "IYL", "", "A"},
    {"b", "c", "d", "e", "iyh", "iyl", "(iy+d)", "a"},
    ^^{ static_cast<std::uint16_t>(regs().get(RegisterFile::R16::IY)
                                   + static_cast<std::int8_t>(read_immediate())) },
    "(iy+d)",
    {"BC", "DE", "IY", "SP"},
    {"BC", "DE", "IY", "AF"},
    "IY",
};

// Condition-code names for cc-field (0..7): nz, z, nc, c, po, pe, p, m.
inline constexpr std::array<std::string_view, 8> cc_names = {
    "nz", "z", "nc", "c", "po", "pe", "p", "m"};

// ============================================================================
// Shape generators. Each takes a list_builder and adds its arms.
// ============================================================================

// 0x40-0x7f minus 0x76: ld r, r' (63 opcodes per RegSet).
//
// Subtle Z80 detail: under DD/FD prefix, the H/L slots (r-field 4/5) are
// re-encoded as IXH/IXL or IYH/IYL — but ONLY when neither operand is the
// indirect (HL) form. When one operand is (IX+d)/(IY+d), the other 8-bit
// operand is the REAL H/L. That's why we keep `hl_regs.r8_ids[...]` for the
// non-indirect side of the mixed case.
consteval void emit_ld_rr(std::meta::list_builder &cases, RegSet const &rs) {
  for (std::size_t d = 0; d < 8; ++d) for (std::size_t s = 0; s < 8; ++s) {
    if (d == 6 && s == 6) continue; // HALT.
    const auto opcode = static_cast<std::uint8_t>(0x40 | (d << 3) | s);

    std::meta::token_sequence body;
    if (s == 6) {
      // ld r, (IX+d) — r is real H/L when prefix is in play.
      body = ^^{
        regs().set(RegisterFile::R8::\(std::meta::id(hl_regs.r8_ids[d])),
                   read(\(rs.indirect_addr)));
      };
    } else if (d == 6) {
      // ld (IX+d), r — r is real H/L.
      body = ^^{
        write(\(rs.indirect_addr),
              regs().get(RegisterFile::R8::\(std::meta::id(hl_regs.r8_ids[s]))));
      };
    } else {
      // ld r, r' — H/L remap to IXH/IXL or IYH/IYL under prefix.
      body = ^^{
        regs().set(RegisterFile::R8::\(std::meta::id(rs.r8_ids[d])),
                   regs().get(RegisterFile::R8::\(std::meta::id(rs.r8_ids[s]))));
      };
    }
    cases += ^^{ case \(opcode): { \(body) break; } };
  }
}

// 0x06, 0x0e, 0x16, 0x1e, 0x26, 0x2e, 0x36, 0x3e: ld r, $nn (8 per RegSet).
consteval void emit_ld_r_imm(std::meta::list_builder &cases, RegSet const &rs) {
  for (std::size_t y = 0; y < 8; ++y) {
    const auto opcode = static_cast<std::uint8_t>(0x06 | (y << 3));
    std::meta::token_sequence body;
    if (y == 6) {
      body = ^^{ write(\(rs.indirect_addr), read_immediate()); };
    } else {
      body = ^^{
        regs().set(RegisterFile::R8::\(std::meta::id(rs.r8_ids[y])),
                   read_immediate());
      };
    }
    cases += ^^{ case \(opcode): { \(body) break; } };
  }
}

// 0x01, 0x11, 0x21, 0x31: ld rp, $nnnn (4 per RegSet).
consteval void emit_ld_rp_imm(std::meta::list_builder &cases, RegSet const &rs) {
  for (std::size_t p = 0; p < 4; ++p) {
    const auto opcode = static_cast<std::uint8_t>(0x01 | (p << 4));
    cases += ^^{
      case \(opcode):
        regs().set(RegisterFile::R16::\(std::meta::id(rs.rp_ids[p])),
                   read_immediate16());
        break;
    };
  }
}

// 0x04/0x0c/.../0x3c: inc r ; 0x05/0x0d/.../0x3d: dec r (8 each per RegSet).
consteval void emit_inc_dec_r(std::meta::list_builder &cases, RegSet const &rs) {
  for (std::size_t y = 0; y < 8; ++y) for (std::size_t dec = 0; dec < 2; ++dec) {
    const auto opcode = static_cast<std::uint8_t>(0x04 | (y << 3) | dec);
    const auto alu = std::meta::token_sequence{dec ? ^^{ Alu::dec8 } : ^^{ Alu::inc8 }};
    std::meta::token_sequence body;
    if (y == 6) {
      body = ^^{
        const auto address = \(rs.indirect_addr);
        const auto value = read(address);
        pass_time(1);
        const auto [result, new_flags] = \(alu)(value, flags());
        write(address, result);
        flags(new_flags);
      };
    } else {
      body = ^^{
        const auto [result, new_flags] =
            \(alu)(regs().get(RegisterFile::R8::\(std::meta::id(rs.r8_ids[y]))), flags());
        regs().set(RegisterFile::R8::\(std::meta::id(rs.r8_ids[y])), result);
        flags(new_flags);
      };
    }
    cases += ^^{ case \(opcode): { \(body) break; } };
  }
}

// 0x03/0x13/0x23/0x33: inc rp ; 0x0b/0x1b/0x2b/0x3b: dec rp (4 each per RegSet).
consteval void emit_inc_dec_rp(std::meta::list_builder &cases, RegSet const &rs) {
  for (std::size_t p = 0; p < 4; ++p) for (std::size_t dec = 0; dec < 2; ++dec) {
    const auto opcode = static_cast<std::uint8_t>(0x03 | (p << 4) | (dec << 3));
    const auto delta = static_cast<std::uint16_t>(dec ? 0xffff : 0x0001);
    cases += ^^{
      case \(opcode):
        regs().set(RegisterFile::R16::\(std::meta::id(rs.rp_ids[p])),
                   static_cast<std::uint16_t>(
                       regs().get(RegisterFile::R16::\(std::meta::id(rs.rp_ids[p])))
                       + \(delta)));
        pass_time(2);
        break;
    };
  }
}

// 0x09, 0x19, 0x29, 0x39: add HL/IX/IY, rp (4 per RegSet).
consteval void emit_add_hl_rp(std::meta::list_builder &cases, RegSet const &rs) {
  for (std::size_t p = 0; p < 4; ++p) {
    const auto opcode = static_cast<std::uint8_t>(0x09 | (p << 4));
    cases += ^^{
      case \(opcode): {
        const auto rhs = regs().get(RegisterFile::R16::\(std::meta::id(rs.rp_ids[p])));
        const auto [result, new_flags] =
            Alu::add16(regs().get(RegisterFile::R16::\(std::meta::id(rs.index16))),
                       rhs, flags());
        regs().set(RegisterFile::R16::\(std::meta::id(rs.index16)), result);
        flags(new_flags);
        pass_time(7);
        break;
      }
    };
  }
}

// 0x80-0xbf: alu a, r (8 ops × 8 sources = 64 per RegSet).
// 0xc6,0xce,0xd6,0xde,0xe6,0xee,0xf6,0xfe: alu a, $nn (8, RegSet-independent).
consteval void emit_alu_a_r(std::meta::list_builder &cases, RegSet const &rs) {
  constexpr std::array<std::meta::token_sequence, 8> alu_exprs = {
      ^^{ Alu::add8(regs().get(RegisterFile::R8::A), rhs, false)              },
      ^^{ Alu::add8(regs().get(RegisterFile::R8::A), rhs, flags().carry())    },
      ^^{ Alu::sub8(regs().get(RegisterFile::R8::A), rhs, false)              },
      ^^{ Alu::sub8(regs().get(RegisterFile::R8::A), rhs, flags().carry())    },
      ^^{ Alu::and8(regs().get(RegisterFile::R8::A), rhs)                     },
      ^^{ Alu::xor8(regs().get(RegisterFile::R8::A), rhs)                     },
      ^^{ Alu::or8 (regs().get(RegisterFile::R8::A), rhs)                     },
      ^^{ Alu::cmp8(regs().get(RegisterFile::R8::A), rhs)                     },
  };

  // x = 2, z = 0..7: source is r-field-encoded.
  for (std::size_t y = 0; y < 8; ++y) for (std::size_t s = 0; s < 8; ++s) {
    const auto opcode = static_cast<std::uint8_t>(0x80 | (y << 3) | s);
    auto rhs_expr = (s == 6) ? ^^{ read(\(rs.indirect_addr)) }
                              : ^^{ regs().get(RegisterFile::R8::\(std::meta::id(rs.r8_ids[s]))) };
    // CP doesn't write back to A.
    auto write_back = (y == 7) ? ^^{} : ^^{ regs().set(RegisterFile::R8::A, result); };
    cases += ^^{
      case \(opcode): {
        const auto rhs = \(rhs_expr);
        const auto [result, new_flags] = \(alu_exprs[y]);
        \(write_back)
        flags(new_flags);
        break;
      }
    };
  }

  // x = 3, z = 6: alu a, $nn (immediate operand).
  for (std::size_t y = 0; y < 8; ++y) {
    const auto opcode = static_cast<std::uint8_t>(0xc6 | (y << 3));
    auto write_back = (y == 7) ? ^^{} : ^^{ regs().set(RegisterFile::R8::A, result); };
    cases += ^^{
      case \(opcode): {
        const auto rhs = read_immediate();
        const auto [result, new_flags] = \(alu_exprs[y]);
        \(write_back)
        flags(new_flags);
        break;
      }
    };
  }
}

// 0xc1/0xd1/0xe1/0xf1: pop rp2 ; 0xc5/0xd5/0xe5/0xf5: push rp2.
consteval void emit_push_pop(std::meta::list_builder &cases, RegSet const &rs) {
  for (std::size_t p = 0; p < 4; ++p) {
    const auto pop = static_cast<std::uint8_t>(0xc1 | (p << 4));
    const auto push = static_cast<std::uint8_t>(0xc5 | (p << 4));
    cases += ^^{
      case \(pop):
        regs().set(RegisterFile::R16::\(std::meta::id(rs.rp2_ids[p])), pop16());
        break;
    };
    cases += ^^{
      case \(push):
        pass_time(1);
        push16(regs().get(RegisterFile::R16::\(std::meta::id(rs.rp2_ids[p]))));
        break;
    };
  }
}

// 0xc2/0xca/.../0xfa: jp cc, $nnnn (8 opcodes).
// 0xc4/0xcc/.../0xfc: call cc, $nnnn (8 opcodes).
// 0xc0/0xc8/.../0xf8: ret cc (8 opcodes).
consteval std::meta::token_sequence cc_check_for(std::size_t y) {
  switch (y) {
    case 0: return ^^{ !flags().zero() };
    case 1: return ^^{  flags().zero() };
    case 2: return ^^{ !flags().carry() };
    case 3: return ^^{  flags().carry() };
    case 4: return ^^{ !flags().parity() };
    case 5: return ^^{  flags().parity() };
    case 6: return ^^{ !flags().sign() };
    default: return ^^{  flags().sign() };
  }
}

consteval void emit_cc_jumps(std::meta::list_builder &cases) {
  for (std::size_t y = 0; y < 8; ++y) {
    const auto jp = static_cast<std::uint8_t>(0xc2 | (y << 3));
    const auto call = static_cast<std::uint8_t>(0xc4 | (y << 3));
    const auto ret = static_cast<std::uint8_t>(0xc0 | (y << 3));
    const auto cc = cc_check_for(y);
    cases += ^^{
      case \(jp): {
        const auto target = read_immediate16();
        if (\(cc)) regs().pc(target);
        break;
      }
    };
    cases += ^^{
      case \(call): {
        const auto target = read_immediate16();
        if (\(cc)) {
          pass_time(1);
          push16(regs().pc());
          regs().pc(target);
        }
        break;
      }
    };
    cases += ^^{
      case \(ret):
        pass_time(1);
        if (\(cc)) regs().pc(pop16());
        break;
    };
  }
}

// 0x20/0x28/0x30/0x38: jr cc, $d (4 opcodes; bit cc encoded in y-4 for y=4..7).
consteval void emit_jr_cc(std::meta::list_builder &cases) {
  for (std::size_t i = 0; i < 4; ++i) {
    const auto opcode = static_cast<std::uint8_t>(0x20 | (i << 3));
    const auto cc = cc_check_for(i); // 0..3: nz/z/nc/c, lines up with the lower half of cc_check_for.
    cases += ^^{
      case \(opcode): {
        const auto offset = static_cast<std::int8_t>(read_immediate());
        if (\(cc)) {
          pass_time(5);
          regs().pc(static_cast<std::uint16_t>(regs().pc() + offset));
        }
        break;
      }
    };
  }
}

// Opcodes that reference HL but are re-prefixed to IX/IY under DD/FD.
// 0x22: ld ($nnnn), HL/IX/IY ; 0x2a: ld HL/IX/IY, ($nnnn) ;
// 0xe3: ex (sp), HL/IX/IY    ; 0xe9: jp (HL/IX/IY)        ;
// 0xf9: ld sp, HL/IX/IY.
consteval void emit_index_ops(std::meta::list_builder &cases, RegSet const &rs) {
  cases += ^^{
    case 0x22: {
      const auto addr = read_immediate16();
      const auto value = regs().get(RegisterFile::R16::\(std::meta::id(rs.index16)));
      write(addr, static_cast<std::uint8_t>(value & 0xff));
      write(static_cast<std::uint16_t>(addr + 1), static_cast<std::uint8_t>(value >> 8));
      break;
    }
  };
  cases += ^^{
    case 0x2a: {
      const auto addr = read_immediate16();
      const auto lo = read(addr);
      const auto hi = read(static_cast<std::uint16_t>(addr + 1));
      regs().set(RegisterFile::R16::\(std::meta::id(rs.index16)),
                 static_cast<std::uint16_t>(lo | (hi << 8)));
      break;
    }
  };
  cases += ^^{
    case 0xe3: {
      const auto sp = regs().sp();
      const auto lo = read(sp);
      pass_time(1);
      const auto hi = read(static_cast<std::uint16_t>(sp + 1));
      const auto value = regs().get(RegisterFile::R16::\(std::meta::id(rs.index16)));
      write(sp, static_cast<std::uint8_t>(value & 0xff));
      pass_time(2);
      write(static_cast<std::uint16_t>(sp + 1), static_cast<std::uint8_t>(value >> 8));
      regs().set(RegisterFile::R16::\(std::meta::id(rs.index16)),
                 static_cast<std::uint16_t>(lo | (hi << 8)));
      break;
    }
  };
  cases += ^^{
    case 0xe9:
      regs().pc(regs().get(RegisterFile::R16::\(std::meta::id(rs.index16))));
      break;
  };
  cases += ^^{
    case 0xf9:
      pass_time(2);
      regs().sp(regs().get(RegisterFile::R16::\(std::meta::id(rs.index16))));
      break;
  };
}

// 0xc7/0xcf/.../0xff: rst $00..$38.
consteval void emit_rst(std::meta::list_builder &cases) {
  for (std::size_t y = 0; y < 8; ++y) {
    const auto opcode = static_cast<std::uint8_t>(0xc7 | (y << 3));
    const auto target = static_cast<std::uint16_t>(y * 8);
    cases += ^^{
      case \(opcode):
        pass_time(1);
        push16(regs().pc());
        regs().pc(\(target));
        break;
    };
  }
}

// Rotate/shift ALU expressions used by both CB and DDCB/FDCB.
consteval std::meta::token_sequence cb_rot_expr(std::size_t y) {
  switch (y) {
    case 0: return ^^{ Alu::rotate_circular8 (lhs, Alu::Direction::Left)                   };
    case 1: return ^^{ Alu::rotate_circular8 (lhs, Alu::Direction::Right)                  };
    case 2: return ^^{ Alu::rotate8          (lhs, Alu::Direction::Left,  flags().carry()) };
    case 3: return ^^{ Alu::rotate8          (lhs, Alu::Direction::Right, flags().carry()) };
    case 4: return ^^{ Alu::shift_arithmetic8(lhs, Alu::Direction::Left)                   };
    case 5: return ^^{ Alu::shift_arithmetic8(lhs, Alu::Direction::Right)                  };
    case 6: return ^^{ Alu::shift_logical8   (lhs, Alu::Direction::Left)                   };
    default:return ^^{ Alu::shift_logical8   (lhs, Alu::Direction::Right)                  };
  }
}

// CB 0x00-0x3f: rotate/shift on r ; 0x40-0x7f: bit ; 0x80-0xbf: res ; 0xc0-0xff: set.
// The indirect form uses regs_.wz() as the address (pre-set by execute_one).
consteval void emit_cb(std::meta::list_builder &cases) {
  for (std::size_t z = 0; z < 8; ++z) {
    const auto load = (z == 6)
        ? ^^{ const auto lhs = read(regs_.wz()); pass_time(1); }
        : ^^{ const auto lhs = regs().get(RegisterFile::R8::\(std::meta::id(hl_regs.r8_ids[z]))); };
    const auto store = (z == 6)
        ? ^^{ write(regs_.wz(), result); }
        : ^^{ regs().set(RegisterFile::R8::\(std::meta::id(hl_regs.r8_ids[z])), result); };
    const auto bus_noise = (z == 6)
        ? ^^{ static_cast<std::uint8_t>(regs_.wz() >> 8) }
        : ^^{ lhs };

    for (std::size_t y = 0; y < 8; ++y) {
      const auto rot = static_cast<std::uint8_t>((y << 3) | z);
      const auto rot_expr = cb_rot_expr(y);
      cases += ^^{
        case \(rot): {
          \(load)
          const auto [result, new_flags] = \(rot_expr);
          \(store)
          flags(new_flags);
          break;
        }
      };
      const auto bit_op = static_cast<std::uint8_t>(0x40 | (y << 3) | z);
      const auto mask = static_cast<std::uint8_t>(1u << y);
      cases += ^^{
        case \(bit_op): {
          \(load)
          flags(Alu::bit(lhs, \(mask), flags(), \(bus_noise)));
          break;
        }
      };
      const auto res_op = static_cast<std::uint8_t>(0x80 | (y << 3) | z);
      const auto res_mask = static_cast<std::uint8_t>(~(1u << y));
      cases += ^^{
        case \(res_op): {
          \(load)
          const auto result = static_cast<std::uint8_t>(lhs & \(res_mask));
          \(store)
          break;
        }
      };
      const auto set_op = static_cast<std::uint8_t>(0xc0 | (y << 3) | z);
      cases += ^^{
        case \(set_op): {
          \(load)
          const auto result = static_cast<std::uint8_t>(lhs | \(mask));
          \(store)
          break;
        }
      };
    }
  }
}

// DDCB/FDCB: like CB but the operand is ALWAYS (IX+d)/(IY+d) (the address
// is pre-set in regs_.wz() before dispatch), AND for non-bit ops (rotate/
// res/set) when z != 6, the result is *also* written into the r-register
// at position z. That's the undocumented "side store" behaviour.
consteval void emit_idxcb(std::meta::list_builder &cases) {
  for (std::size_t z = 0; z < 8; ++z) {
    const auto side_store = (z == 6) ? std::meta::token_sequence{^^{}}
        : ^^{ regs().set(RegisterFile::R8::\(std::meta::id(hl_regs.r8_ids[z])), result); };

    for (std::size_t y = 0; y < 8; ++y) {
      const auto rot = static_cast<std::uint8_t>((y << 3) | z);
      const auto rot_expr = cb_rot_expr(y);
      cases += ^^{
        case \(rot): {
          const auto lhs = read(regs_.wz());
          pass_time(1);
          const auto [result, new_flags] = \(rot_expr);
          write(regs_.wz(), result);
          flags(new_flags);
          \(side_store)
          break;
        }
      };
      const auto bit_op = static_cast<std::uint8_t>(0x40 | (y << 3) | z);
      const auto mask = static_cast<std::uint8_t>(1u << y);
      cases += ^^{
        case \(bit_op): {
          const auto lhs = read(regs_.wz());
          pass_time(1);
          flags(Alu::bit(lhs, \(mask), flags(),
                         static_cast<std::uint8_t>(regs_.wz() >> 8)));
          break;
        }
      };
      const auto res_op = static_cast<std::uint8_t>(0x80 | (y << 3) | z);
      const auto res_mask = static_cast<std::uint8_t>(~(1u << y));
      cases += ^^{
        case \(res_op): {
          const auto lhs = read(regs_.wz());
          pass_time(1);
          const auto result = static_cast<std::uint8_t>(lhs & \(res_mask));
          write(regs_.wz(), result);
          \(side_store)
          break;
        }
      };
      const auto set_op = static_cast<std::uint8_t>(0xc0 | (y << 3) | z);
      cases += ^^{
        case \(set_op): {
          const auto lhs = read(regs_.wz());
          pass_time(1);
          const auto result = static_cast<std::uint8_t>(lhs | \(mask));
          write(regs_.wz(), result);
          \(side_store)
          break;
        }
      };
    }
  }
}

// ED-prefixed opcodes. Mostly one-offs, but enough patterns to use loops.
consteval void emit_ed(std::meta::list_builder &cases) {
  // 0x40,48,...,78: in r, (C) ; 0x41,49,...,79: out (C), r.
  for (std::size_t y = 0; y < 8; ++y) {
    const auto in_op = static_cast<std::uint8_t>(0x40 | (y << 3));
    const auto out_op = static_cast<std::uint8_t>(0x41 | (y << 3));
    const auto in_store = (y == 6) ? std::meta::token_sequence{^^{}}
        : ^^{ regs().set(RegisterFile::R8::\(std::meta::id(hl_regs.r8_ids[y])), result); };
    const auto out_src = (y == 6) ? std::meta::token_sequence{^^{ static_cast<std::uint8_t>(0) }}
        : ^^{ regs().get(RegisterFile::R8::\(std::meta::id(hl_regs.r8_ids[y]))) };
    cases += ^^{
      case \(in_op): {
        pass_time(4);
        const auto result = in(regs().get(RegisterFile::R16::BC));
        flags((flags() & Flags::Carry()) | Alu::parity_flags_for(result));
        \(in_store)
        break;
      }
    };
    cases += ^^{
      case \(out_op): {
        pass_time(4);
        out(regs().get(RegisterFile::R16::BC), \(out_src));
        break;
      }
    };
  }

  // 0x42,52,62,72: SBC HL, rp ; 0x4a,5a,6a,7a: ADC HL, rp.
  for (std::size_t p = 0; p < 4; ++p) {
    const auto sbc = static_cast<std::uint8_t>(0x42 | (p << 4));
    const auto adc = static_cast<std::uint8_t>(0x4a | (p << 4));
    cases += ^^{
      case \(sbc): {
        const auto [result, new_flags] = Alu::sbc16(
            regs().get(RegisterFile::R16::HL),
            regs().get(RegisterFile::R16::\(std::meta::id(hl_regs.rp_ids[p]))),
            flags().carry());
        regs().set(RegisterFile::R16::HL, result);
        flags(new_flags);
        pass_time(7);
        break;
      }
    };
    cases += ^^{
      case \(adc): {
        const auto [result, new_flags] = Alu::adc16(
            regs().get(RegisterFile::R16::HL),
            regs().get(RegisterFile::R16::\(std::meta::id(hl_regs.rp_ids[p]))),
            flags().carry());
        regs().set(RegisterFile::R16::HL, result);
        flags(new_flags);
        pass_time(7);
        break;
      }
    };
  }

  // 0x43,53,63,73: LD ($nnnn), rp ; 0x4b,5b,6b,7b: LD rp, ($nnnn).
  constexpr std::array<std::string_view, 4> rp_hi = {"B", "D", "H", "SPH"};
  constexpr std::array<std::string_view, 4> rp_lo = {"C", "E", "L", "SPL"};
  for (std::size_t p = 0; p < 4; ++p) {
    const auto st = static_cast<std::uint8_t>(0x43 | (p << 4));
    const auto ld = static_cast<std::uint8_t>(0x4b | (p << 4));
    cases += ^^{
      case \(st): {
        const auto addr = read_immediate16();
        write(addr,
              regs().get(RegisterFile::R8::\(std::meta::id(rp_lo[p]))));
        write(static_cast<std::uint16_t>(addr + 1),
              regs().get(RegisterFile::R8::\(std::meta::id(rp_hi[p]))));
        break;
      }
    };
    cases += ^^{
      case \(ld): {
        const auto addr = read_immediate16();
        regs().set(RegisterFile::R8::\(std::meta::id(rp_lo[p])), read(addr));
        regs().set(RegisterFile::R8::\(std::meta::id(rp_hi[p])),
                   read(static_cast<std::uint16_t>(addr + 1)));
        break;
      }
    };
  }

  // 0x44,4c,54,5c,64,6c,74,7c: NEG (all eight encodings).
  for (std::size_t y = 0; y < 8; ++y) {
    const auto op = static_cast<std::uint8_t>(0x44 | (y << 3));
    cases += ^^{
      case \(op): {
        const auto [result, new_flags] = Alu::sub8(0, regs().get(RegisterFile::R8::A), false);
        regs().set(RegisterFile::R8::A, result);
        flags(new_flags);
        break;
      }
    };
  }

  // 0x45,55,65,75: RETN ; 0x4d,5d,6d,7d: RETI (treated the same here).
  for (std::size_t y = 0; y < 8; ++y) {
    const auto op = static_cast<std::uint8_t>(0x45 | (y << 3));
    cases += ^^{
      case \(op): {
        iff1_ = iff2_;
        regs().pc(pop16());
        break;
      }
    };
  }

  // 0x46/56/66/76 (y=0/2/4/6): IM 0/?/1/2 (with extra encodings via odd y).
  for (std::size_t y = 0; y < 8; ++y) {
    const auto op = static_cast<std::uint8_t>(0x46 | (y << 3));
    constexpr std::array<std::uint8_t, 8> modes{0, 0, 1, 2, 0, 0, 1, 2};
    const auto mode = modes[y];
    cases += ^^{
      case \(op): {
        irq_mode(\(mode));
        break;
      }
    };
  }

  // 0x47: LD I, A ; 0x4F: LD R, A ; 0x57: LD A, I ; 0x5F: LD A, R ; 0x67: RRD ; 0x6F: RLD.
  cases += ^^{
    case 0x47: pass_time(1); regs_.i(regs().get(RegisterFile::R8::A)); break;
  };
  cases += ^^{
    case 0x4f: pass_time(1); regs_.r(regs().get(RegisterFile::R8::A)); break;
  };
  cases += ^^{
    case 0x57: {
      pass_time(1);
      regs().set(RegisterFile::R8::A, regs_.i());
      flags(Alu::iff2_flags_for(regs_.i(), flags(), iff2_));
      break;
    }
  };
  cases += ^^{
    case 0x5f: {
      pass_time(1);
      regs().set(RegisterFile::R8::A, regs_.r());
      flags(Alu::iff2_flags_for(regs_.r(), flags(), iff2_));
      break;
    }
  };
  cases += ^^{
    case 0x67: { // RRD
      const auto address = regs().get(RegisterFile::R16::HL);
      const auto ind_hl = read(address);
      const auto prev_a = regs().get(RegisterFile::R8::A);
      const auto new_a = static_cast<std::uint8_t>((prev_a & 0xf0) | (ind_hl & 0xf));
      regs().set(RegisterFile::R8::A, new_a);
      pass_time(4);
      write(address, static_cast<std::uint8_t>((ind_hl >> 4) | ((prev_a & 0xf) << 4)));
      flags((flags() & Flags::Carry()) | Alu::parity_flags_for(new_a));
      break;
    }
  };
  cases += ^^{
    case 0x6f: { // RLD
      const auto address = regs().get(RegisterFile::R16::HL);
      const auto ind_hl = read(address);
      const auto prev_a = regs().get(RegisterFile::R8::A);
      const auto new_a = static_cast<std::uint8_t>((prev_a & 0xf0) | ((ind_hl >> 4) & 0xf));
      regs().set(RegisterFile::R8::A, new_a);
      pass_time(4);
      write(address, static_cast<std::uint8_t>((ind_hl << 4) | (prev_a & 0xf)));
      flags((flags() & Flags::Carry()) | Alu::parity_flags_for(new_a));
      break;
    }
  };

  // Block ops: 0xA0/A1/A8/A9 + repeat variants 0xB0/B1/B8/B9.
  // LDI/LDIR/LDD/LDDR (z=0) and CPI/CPIR/CPD/CPDR (z=1).
  for (std::size_t y = 4; y < 8; ++y) {
    const bool increment = !(y & 1);
    const bool repeat = (y & 2) != 0;
    const auto delta = static_cast<std::uint16_t>(increment ? 0x0001 : 0xffff);

    // LDI/LDIR/LDD/LDDR
    const auto ld_op = static_cast<std::uint8_t>(0xa0 | ((y - 4) << 3));
    const auto ld_repeat = repeat ? ^^{
        if (new_bc != 0) {
          regs_.wz(static_cast<std::uint16_t>(regs().pc() - 1));
          regs().pc(static_cast<std::uint16_t>(regs().pc() - 2));
          pass_time(5);
        }
      } : std::meta::token_sequence{^^{}};
    cases += ^^{
      case \(ld_op): {
        const auto source = regs().get(RegisterFile::R16::HL);
        regs().set(RegisterFile::R16::HL, static_cast<std::uint16_t>(source + \(delta)));
        const auto byte = read(source);
        const auto dest = regs().get(RegisterFile::R16::DE);
        regs().set(RegisterFile::R16::DE, static_cast<std::uint16_t>(dest + \(delta)));
        write(dest, byte);
        pass_time(2);
        const auto flag_bits = static_cast<std::uint8_t>(byte + regs().get(RegisterFile::R8::A));
        const auto new_bc = static_cast<std::uint16_t>(regs().get(RegisterFile::R16::BC) - 1);
        regs().set(RegisterFile::R16::BC, new_bc);
        const auto preserved = flags() & (Flags::Sign() | Flags::Zero() | Flags::Carry());
        const auto fbits = ((flag_bits & 0x08) ? Flags::Flag3() : Flags())
                         | ((flag_bits & 0x02) ? Flags::Flag5() : Flags());
        const auto fbc = new_bc ? Flags::Overflow() : Flags();
        flags(preserved | fbits | fbc);
        \(ld_repeat)
        break;
      }
    };

    // CPI/CPIR/CPD/CPDR
    const auto cp_op = static_cast<std::uint8_t>(0xa1 | ((y - 4) << 3));
    const auto cp_repeat = repeat ? ^^{
        if (new_bc != 0 && !subtract_flags.zero()) {
          regs_.wz(static_cast<std::uint16_t>(regs().pc() - 1));
          regs().pc(static_cast<std::uint16_t>(regs().pc() - 2));
          pass_time(5);
        }
      } : std::meta::token_sequence{^^{}};
    cases += ^^{
      case \(cp_op): {
        const auto source = regs().get(RegisterFile::R16::HL);
        regs().set(RegisterFile::R16::HL, static_cast<std::uint16_t>(source + \(delta)));
        const auto byte = read(source);
        const auto [result, subtract_flags] = Alu::sub8(regs().get(RegisterFile::R8::A), byte, false);
        pass_time(5);
        const auto flag_bits = subtract_flags.half_carry()
            ? static_cast<std::uint8_t>(result - 1) : result;
        const auto new_bc = static_cast<std::uint16_t>(regs().get(RegisterFile::R16::BC) - 1);
        regs().set(RegisterFile::R16::BC, new_bc);
        constexpr auto from_sub = Flags::HalfCarry() | Flags::Zero() | Flags::Sign() | Flags::Subtract();
        const auto preserved = flags() & ~(Flags::Flag3() | Flags::Flag5() | from_sub | Flags::Overflow());
        const auto fbits = ((flag_bits & 0x08) ? Flags::Flag3() : Flags())
                         | ((flag_bits & 0x02) ? Flags::Flag5() : Flags());
        const auto fbc = new_bc ? Flags::Overflow() : Flags();
        flags(preserved | fbits | fbc | (from_sub & subtract_flags));
        \(cp_repeat)
        break;
      }
    };
  }
}

// ============================================================================
// Dispatcher injector. Composes shapes + simple/base opcodes into one switch.
// ============================================================================

enum class BaseOpcodes { Skip, Include };

consteval void inject_dispatcher(std::string_view fn_name, RegSet const &rs,
                                 BaseOpcodes base) {
  std::meta::list_builder cases;
  if (base == BaseOpcodes::Include) {
    for (auto const &i : base_instructions) {
      cases += ^^{ case \(i.opcode): { \(i.body) break; } };
    }
  }
  emit_ld_rr      (cases, rs);
  emit_ld_r_imm   (cases, rs);
  emit_ld_rp_imm  (cases, rs);
  emit_inc_dec_r  (cases, rs);
  emit_inc_dec_rp (cases, rs);
  emit_add_hl_rp  (cases, rs);
  emit_alu_a_r    (cases, rs);
  emit_push_pop   (cases, rs);
  emit_index_ops  (cases, rs);  // HL/IX/IY-referencing opcodes (0x22, 0x2a, 0xe3, 0xe9, 0xf9).
  if (base == BaseOpcodes::Include) {
    emit_cc_jumps(cases);
    emit_jr_cc   (cases);
    emit_rst     (cases);
  }
  std::meta::queue_injection(^^{
    void \(std::meta::id(fn_name))(std::uint8_t opcode) {
      switch (opcode) {
        \(cases)
        default: break;
      }
    }
  });
}

// CB-prefixed dispatcher (operand at regs_.wz()).
consteval void inject_cb_dispatcher(std::string_view fn_name) {
  std::meta::list_builder cases;
  emit_cb(cases);
  std::meta::queue_injection(^^{
    void \(std::meta::id(fn_name))(std::uint8_t opcode) {
      switch (opcode) { \(cases) default: break; }
    }
  });
}

// DDCB/FDCB dispatcher (operand always at regs_.wz()).
consteval void inject_idxcb_dispatcher(std::string_view fn_name) {
  std::meta::list_builder cases;
  emit_idxcb(cases);
  std::meta::queue_injection(^^{
    void \(std::meta::id(fn_name))(std::uint8_t opcode) {
      switch (opcode) { \(cases) default: break; }
    }
  });
}

// ED-prefixed dispatcher.
consteval void inject_ed_dispatcher(std::string_view fn_name) {
  std::meta::list_builder cases;
  emit_ed(cases);
  std::meta::queue_injection(^^{
    void \(std::meta::id(fn_name))(std::uint8_t opcode) {
      switch (opcode) { \(cases) default: break; }
    }
  });
}

} // namespace specbolt::v4
