#pragma once

// Parameterised instruction shapes. A "shape" is a consteval function that
// expands one declarative rule into many case arms in the dispatcher's
// switch. e.g. emit_ld_rr() emits all 63 `ld r, r'` opcodes.

#include "z80/common/RegisterFile.hpp"

#include <array>
#include <cstdint>
#include <meta>
#include <string_view>

namespace specbolt::v4 {

// Z80 r-encoding: 3-bit field → register/operand identity.
// Indexed by the encoding bits, 6 is the indirect (HL) form.
inline constexpr std::array<std::string_view, 8> r8_ids = {
    "B", "C", "D", "E", "H", "L", "" /* indirect, handled separately */, "A"};

inline constexpr std::array<std::string_view, 8> r8_disp = {
    "b", "c", "d", "e", "h", "l", "(hl)", "a"};

// Emit all 63 `ld r, r'` opcodes (0x40-0x7f minus 0x76 which is HALT).
// One shape, three body variants depending on whether either operand
// is the indirect (HL) form.
consteval void emit_ld_rr(std::meta::list_builder &cases) {
  for (std::size_t d = 0; d < 8; ++d) for (std::size_t s = 0; s < 8; ++s) {
    if (d == 6 && s == 6) continue; // HALT, not a load.

    const auto opcode = static_cast<std::uint8_t>(0x40 | (d << 3) | s);

    std::meta::token_sequence body;
    if (s == 6) {
      // ld r, (hl)
      body = ^^{
        regs().set(RegisterFile::R8::\(std::meta::id(r8_ids[d])),
                   read(regs().get(RegisterFile::R16::HL)));
      };
    } else if (d == 6) {
      // ld (hl), r
      body = ^^{
        write(regs().get(RegisterFile::R16::HL),
              regs().get(RegisterFile::R8::\(std::meta::id(r8_ids[s]))));
      };
    } else {
      // ld r, r'
      body = ^^{
        regs().set(RegisterFile::R8::\(std::meta::id(r8_ids[d])),
                   regs().get(RegisterFile::R8::\(std::meta::id(r8_ids[s]))));
      };
    }

    cases += ^^{ case \(opcode): \(body) break; };
  }
}

} // namespace specbolt::v4
