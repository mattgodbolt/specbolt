#ifndef SPECBOLT_MODULES
#include "z80/v4/Disassembler.hpp"

#include "Table.hpp"
#include "peripherals/Memory.hpp"

#include <format>
#include <string>
#endif

namespace specbolt::v4 {

Disassembled disassemble(const Memory &memory, const std::uint16_t address) {
  const auto byte_at = [&](const std::size_t offset) {
    return memory.read(static_cast<std::uint16_t>(address + offset));
  };

  // Follow prefixes until a row that renders something is reached.
  std::size_t offset = 0;
  auto table = entry_table;
  const Row *row = nullptr;
  while (true) {
    const auto index = find_row(table, byte_at(offset));
    ++offset;
    if (!index)
      return {"??", offset};
    row = &rows[*index];
    const auto next = transfers_to(*row);
    if (!next)
      break;
    table = *next;
  }

  std::string result;
  const auto opcode = byte_at(offset - 1);
  for (std::size_t piece = 0; piece < row->pieces.size(); ++piece) {
    const auto &part = row->pieces[piece];
    switch (part.kind) {
      case Piece::Kind::Literal: result += part.text; break;
      case Piece::Kind::Field: result += member_of(fields, part.reference, row->matched, opcode).display; break;
      case Piece::Kind::Imm8:
        result += std::format("0x{:02x}", byte_at(offset));
        offset += 1;
        break;
      case Piece::Kind::Imm16: {
        result += std::format("0x{:04x}", static_cast<std::uint16_t>(byte_at(offset) | byte_at(offset + 1) << 8));
        offset += 2;
        break;
      }
    }
  }
  return {result, offset};
}

} // namespace specbolt::v4
