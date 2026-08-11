#ifndef SPECBOLT_MODULES
#include "z80/v4/Disassembler.hpp"

#include "Table.hpp"
#include "peripherals/Memory.hpp"

#include <format>
#include <string>
#endif

namespace specbolt::v4 {

Disassembled disassemble(const Memory &memory, const std::uint16_t address) {
  const auto opcode = memory.read(address);
  const auto index = find_row(opcode);
  if (!index)
    return {"??", 1};

  const auto &row = rows[*index];
  std::string result;
  std::size_t offset = 1;
  for (std::size_t piece = 0; piece < row.num_pieces; ++piece) {
    const auto &part = row.pieces[piece];
    switch (part.kind) {
      case Piece::Kind::Literal: result += part.text; break;
      case Piece::Kind::Field:
        result += fields[part.field_index].values[row.matched.slices[part.slice_index].extract(opcode)];
        break;
      case Piece::Kind::Imm8:
        result += std::format("0x{:02x}", memory.read(static_cast<std::uint16_t>(address + offset)));
        offset += 1;
        break;
      case Piece::Kind::Imm16: {
        const auto low = memory.read(static_cast<std::uint16_t>(address + offset));
        const auto high = memory.read(static_cast<std::uint16_t>(address + offset + 1));
        result += std::format("0x{:04x}", static_cast<std::uint16_t>(low | high << 8));
        offset += 2;
        break;
      }
    }
  }
  return {result, row.length};
}

} // namespace specbolt::v4
