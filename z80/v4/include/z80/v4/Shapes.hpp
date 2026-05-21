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
consteval void emit_ld_rr(std::meta::list_builder &cases, RegSet const &rs) {
  for (std::size_t d = 0; d < 8; ++d) for (std::size_t s = 0; s < 8; ++s) {
    if (d == 6 && s == 6) continue; // HALT.
    const auto opcode = static_cast<std::uint8_t>(0x40 | (d << 3) | s);

    std::meta::token_sequence body;
    if (s == 6) {
      body = ^^{
        regs().set(RegisterFile::R8::\(std::meta::id(rs.r8_ids[d])),
                   read(\(rs.indirect_addr)));
      };
    } else if (d == 6) {
      body = ^^{
        write(\(rs.indirect_addr),
              regs().get(RegisterFile::R8::\(std::meta::id(rs.r8_ids[s]))));
      };
    } else {
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

// CB 0x00-0x3f: rotate/shift ops on r (8 ops × 8 operands = 64 per RegSet).
// CB 0x40-0x7f: bit b, r (8 bits × 8 operands).
// CB 0x80-0xbf: res b, r.
// CB 0xc0-0xff: set b, r.
consteval void emit_cb(std::meta::list_builder &cases, RegSet const &rs) {
  constexpr std::array<std::meta::token_sequence, 8> rot_exprs = {
      ^^{ Alu::rotate_circular8(lhs, Alu::Direction::Left)                  },
      ^^{ Alu::rotate_circular8(lhs, Alu::Direction::Right)                 },
      ^^{ Alu::rotate8         (lhs, Alu::Direction::Left,  flags().carry()) },
      ^^{ Alu::rotate8         (lhs, Alu::Direction::Right, flags().carry()) },
      ^^{ Alu::shift_arithmetic8(lhs, Alu::Direction::Left)                  },
      ^^{ Alu::shift_arithmetic8(lhs, Alu::Direction::Right)                 },
      ^^{ Alu::shift_logical8   (lhs, Alu::Direction::Left)                  },
      ^^{ Alu::shift_logical8   (lhs, Alu::Direction::Right)                 },
  };

  for (std::size_t z = 0; z < 8; ++z) {
    auto load = (z == 6) ? ^^{ const auto lhs = read(\(rs.indirect_addr)); pass_time(1); }
                          : ^^{ const auto lhs = regs().get(RegisterFile::R8::\(std::meta::id(rs.r8_ids[z]))); };
    auto store = (z == 6) ? ^^{ write(\(rs.indirect_addr), result); }
                           : ^^{ regs().set(RegisterFile::R8::\(std::meta::id(rs.r8_ids[z])), result); };

    // x = 0: rotate / shift
    for (std::size_t y = 0; y < 8; ++y) {
      const auto opcode = static_cast<std::uint8_t>((y << 3) | z);
      cases += ^^{
        case \(opcode): {
          \(load)
          const auto [result, new_flags] = \(rot_exprs[y]);
          \(store)
          flags(new_flags);
          break;
        }
      };
    }
    // x = 1: bit b, r — read only, sets flags. Alu::bit takes a MASK, not an index.
    for (std::size_t y = 0; y < 8; ++y) {
      const auto opcode = static_cast<std::uint8_t>(0x40 | (y << 3) | z);
      const auto mask = static_cast<std::uint8_t>(1u << y);
      cases += ^^{
        case \(opcode): {
          \(load)
          flags(Alu::bit(lhs, \(mask), flags(), static_cast<std::uint8_t>(lhs)));
          break;
        }
      };
    }
    // x = 2: res b, r — clear bit.
    for (std::size_t y = 0; y < 8; ++y) {
      const auto opcode = static_cast<std::uint8_t>(0x80 | (y << 3) | z);
      const auto mask = static_cast<std::uint8_t>(~(1u << y));
      cases += ^^{
        case \(opcode): {
          \(load)
          const auto result = static_cast<std::uint8_t>(lhs & \(mask));
          \(store)
          break;
        }
      };
    }
    // x = 3: set b, r — set bit.
    for (std::size_t y = 0; y < 8; ++y) {
      const auto opcode = static_cast<std::uint8_t>(0xc0 | (y << 3) | z);
      const auto mask = static_cast<std::uint8_t>(1u << y);
      cases += ^^{
        case \(opcode): {
          \(load)
          const auto result = static_cast<std::uint8_t>(lhs | \(mask));
          \(store)
          break;
        }
      };
    }
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

// CB-prefixed dispatcher is its own shape (just the rotate/bit/res/set block).
consteval void inject_cb_dispatcher(std::string_view fn_name, RegSet const &rs) {
  std::meta::list_builder cases;
  emit_cb(cases, rs);
  std::meta::queue_injection(^^{
    void \(std::meta::id(fn_name))(std::uint8_t opcode) {
      switch (opcode) {
        \(cases)
        default: break;
      }
    }
  });
}

} // namespace specbolt::v4
