#ifndef SPECBOLT_MODULES
#include "z80/v4/Disassembler.hpp"

#include "Table.hpp"
#include "peripherals/Memory.hpp"

#include <format>
#include <optional>
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
  std::optional<unsigned> latch;
  while (true) {
    const auto index = find_row(table, byte_at(offset));
    ++offset;
    if (!index)
      return {"??", offset};
    row = &rows[*index];
    // `dd cb d op`: the displacement comes between the prefix and the byte that
    // says what to do, so it is taken here rather than after the opcode.
    if (row->reads_displacement)
      latch = byte_at(offset++);
    const auto next = transfers_to(*row);
    if (!next)
      break;
    table = *next;
  }

  std::string result;
  const auto opcode = byte_at(offset - 1);
  // Only an inherited row is renamed; see the note in Execute.hpp.
  const auto &rules = row->table == table ? no_rules : tables[table].rules;
  // The displacement precedes any immediate, so it is taken before the pieces
  // are walked and whatever they read follows it -- unless a prefix already did.
  const auto displaced = displaced_through(fields, *row, opcode, rules);
  const unsigned displacement = latch ? *latch : displaced ? byte_at(offset) : 0;
  if (displaced && !latch)
    ++offset;

  const auto render = [&](const Piece &part) {
    switch (part.kind) {
      case Piece::Kind::Literal: result += part.text; break;
      case Piece::Kind::Field: break; // only the caller can follow one
      case Piece::Kind::Displacement:
        result += displacement < 0x80 ? std::format("+0x{:02x}", displacement)
                                      : std::format("-0x{:02x}", 0x100 - displacement);
        break;
      case Piece::Kind::Imm8:
        result += std::format("0x{:02x}", byte_at(offset));
        offset += 1;
        break;
      case Piece::Kind::Imm16:
        result += std::format("0x{:04x}", static_cast<std::uint16_t>(byte_at(offset) | byte_at(offset + 1) << 8));
        offset += 2;
        break;
    }
  };

  for (const auto &part: row->pieces) {
    if (part.kind != Piece::Kind::Field) {
      render(part);
      continue;
    }
    // A member renders itself, because an indexed mode writes its displacement
    // in the middle of its own text.
    for (const auto &inner: member_of(fields, part.reference, row->matched, opcode, rules).pieces)
      render(inner);
  }
  return {result, offset};
}

} // namespace specbolt::v4
