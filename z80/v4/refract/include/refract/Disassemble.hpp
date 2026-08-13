#pragma once

// The description's other artefact. `Execute.hpp` turns a parsed table into an
// interpreter; this turns the same table into text, from the same rows, the
// same vocabularies and the same lowered pieces.
//
// Nothing here is any particular CPU's, and nothing here parses anything: a
// mnemonic was split into `Piece`s at parse time, so rendering one is walking a
// list. All a machine supplies is where the bytes come from.

#include "refract/Coverage.hpp"
#include "refract/Model.hpp"

#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace specbolt::refract {

struct Disassembly {
  std::string text;
  // In bytes, which is how far the caller advances to reach the next one.
  std::size_t length{};
};

// `byte_at(n)` is the nth byte of the instruction, counting from `address`.
// `address` itself is needed because a relative jump renders where it lands
// rather than how far it goes.
[[nodiscard]] inline Disassembly disassemble(
    const Description &description, const std::uint16_t address, const auto &byte_at) {
  // Follow prefixes until a row that renders something is reached. `dd cb d op`
  // takes its displacement between the prefix and the byte that says what to
  // do, so the latch is filled inside this loop rather than after it.
  std::size_t offset = 0;
  auto table = description.entry;
  const Row *row = nullptr;
  std::optional<unsigned> latch;
  while (true) {
    row = description.row_for(table, byte_at(offset));
    ++offset;
    if (!row)
      return {"??", offset};
    if (row->reads_displacement)
      latch = byte_at(offset++);
    const auto next = transfers_to(*row);
    if (!next)
      break;
    table = *next;
  }

  const auto opcode = byte_at(offset - 1);
  // The table a row was *decoded in* owns the renaming, which is why this asks
  // the table index rather than `row->table`: an inherited row renders under
  // the rules of whoever inherited it.
  const auto &rules = description.rules_for(table);
  // The displacement precedes any immediate, so it is taken before the pieces
  // are walked and whatever they read follows it -- unless a prefix already did.
  const auto displaced = displaced_through(description.vocabularies, *row, opcode, rules);
  const unsigned displacement = latch ? *latch : displaced ? byte_at(offset) : 0;
  if (displaced && !latch)
    ++offset;

  std::string result;
  const auto render = [&](const Piece &part) {
    switch (part.kind) {
      case Piece::Kind::Literal: result += part.text; break;
      case Piece::Kind::Vocabulary: break; // only the caller can follow one
      case Piece::Kind::Displacement:
        result += displacement < 0x80 ? std::format("+0x{:02x}", displacement)
                                      : std::format("-0x{:02x}", 0x100 - displacement);
        break;
      case Piece::Kind::Imm8:
        result += std::format("0x{:02x}", byte_at(offset));
        offset += 1;
        break;
      case Piece::Kind::Relative: {
        // Measured from the byte after the offset, which is the end of the
        // instruction: a relative jump never carries anything else.
        const auto to = static_cast<std::int8_t>(byte_at(offset));
        offset += 1;
        result += std::format("0x{:04x}", static_cast<std::uint16_t>(address + offset + to));
        break;
      }
      case Piece::Kind::Imm16:
        result += std::format("0x{:04x}", static_cast<std::uint16_t>(byte_at(offset) | byte_at(offset + 1) << 8));
        offset += 2;
        break;
    }
  };

  for (const auto &part: row->pieces) {
    if (part.kind != Piece::Kind::Vocabulary) {
      render(part);
      continue;
    }
    // A member renders itself, because an indexed mode writes its displacement
    // in the middle of its own text.
    for (const auto &inner: member_of(description.vocabularies, part.reference, row->matched, opcode, rules).pieces)
      render(inner);
  }
  return {result, offset};
}

} // namespace specbolt::refract
