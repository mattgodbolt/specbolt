#pragma once

// Parameterised instruction shapes. A "shape" is a consteval function that
// expands one declarative rule into many case arms in the dispatcher's
// switch. e.g. emit_ld_rr() emits all 63 `ld r, r'` opcodes.
//
// RegSet captures everything that varies under DD/FD prefix re-encoding,
// so a single emit_ld_rr can populate the base, dd, and fd dispatchers
// from one generator.

#include "z80/common/RegisterFile.hpp"
#include "z80/v4/Instructions.hpp"

#include <array>
#include <cstdint>
#include <meta>
#include <string_view>

namespace specbolt::v4 {

struct RegSet {
  // R8 enumerator names for the r-field encoding 0..7 (slot 6 unused; the
  // indirect form is handled via `indirect_addr` instead).
  std::array<std::string_view, 8> r8_ids;
  // Display names for mnemonics ("b", "ixh", "(ix+d)", ...).
  std::array<std::string_view, 8> r8_disp;
  // Token sequence yielding the indirect address as std::uint16_t.
  std::meta::token_sequence indirect_addr;
  std::string_view indirect_disp;
};

// Base set: no prefix. (HL) is the indirect form.
inline constexpr RegSet hl_regs{
    {"B", "C", "D", "E", "H", "L", "", "A"},
    {"b", "c", "d", "e", "h", "l", "(hl)", "a"},
    ^^{ regs().get(RegisterFile::R16::HL) },
    "(hl)",
};

// Under DD prefix: H/L become IXH/IXL; (HL) becomes (IX+d).
inline constexpr RegSet ix_regs{
    {"B", "C", "D", "E", "IXH", "IXL", "", "A"},
    {"b", "c", "d", "e", "ixh", "ixl", "(ix+d)", "a"},
    ^^{ static_cast<std::uint16_t>(regs().get(RegisterFile::R16::IX)
                                   + static_cast<std::int8_t>(read_immediate())) },
    "(ix+d)",
};

// Under FD prefix: H/L become IYH/IYL; (HL) becomes (IY+d).
inline constexpr RegSet iy_regs{
    {"B", "C", "D", "E", "IYH", "IYL", "", "A"},
    {"b", "c", "d", "e", "iyh", "iyl", "(iy+d)", "a"},
    ^^{ static_cast<std::uint16_t>(regs().get(RegisterFile::R16::IY)
                                   + static_cast<std::int8_t>(read_immediate())) },
    "(iy+d)",
};

// Emit all 63 `ld r, r'` opcodes (0x40-0x7f minus 0x76 HALT). The same
// generator handles base/DD/FD by reading from the supplied RegSet.
consteval void emit_ld_rr(std::meta::list_builder &cases, RegSet const &rs) {
  for (std::size_t d = 0; d < 8; ++d) for (std::size_t s = 0; s < 8; ++s) {
    if (d == 6 && s == 6) continue; // HALT, not a load.

    const auto opcode = static_cast<std::uint8_t>(0x40 | (d << 3) | s);

    std::meta::token_sequence body;
    if (s == 6) {
      // ld r, <indirect>
      body = ^^{
        regs().set(RegisterFile::R8::\(std::meta::id(rs.r8_ids[d])),
                   read(\(rs.indirect_addr)));
      };
    } else if (d == 6) {
      // ld <indirect>, r
      body = ^^{
        write(\(rs.indirect_addr),
              regs().get(RegisterFile::R8::\(std::meta::id(rs.r8_ids[s]))));
      };
    } else {
      // ld r, r'
      body = ^^{
        regs().set(RegisterFile::R8::\(std::meta::id(rs.r8_ids[d])),
                   regs().get(RegisterFile::R8::\(std::meta::id(rs.r8_ids[s]))));
      };
    }

    cases += ^^{ case \(opcode): \(body) break; };
  }
}

// Whether a dispatcher should include the plain non-parameterised opcodes
// from `base_instructions` (only the base/unprefixed dispatcher does — under
// DD/FD prefix, e.g. 0x00 isn't `nop`, it's a prefixed-then-nop with
// different timing).
enum class BaseOpcodes { Skip, Include };

// Build a dispatcher function: collect the base/simple per-opcode arms
// (when requested) plus the LdRR shape, then queue_inject the switch as a
// member with the given name.
consteval void inject_dispatcher(std::string_view fn_name, RegSet const &rs,
                                 BaseOpcodes base) {
  std::meta::list_builder cases;
  if (base == BaseOpcodes::Include) {
    for (auto const &i : base_instructions) {
      cases += ^^{ case \(i.opcode): \(i.body) break; };
    }
  }
  emit_ld_rr(cases, rs);
  std::meta::queue_injection(^^{
    void \(std::meta::id(fn_name))(std::uint8_t opcode) {
      switch (opcode) {
        \(cases)
        default: break; // TODO: unimplemented opcode
      }
    }
  });
}

} // namespace specbolt::v4
