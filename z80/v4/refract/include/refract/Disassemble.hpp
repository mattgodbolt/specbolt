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
#include <functional>
#include <optional>
#include <string>

namespace specbolt::refract {

// One instruction rendered: its text, and how many bytes it occupied.
struct Disassembly {
  std::string text;
  // In bytes, which is how far the caller advances to reach the next one.
  std::size_t length{};
};

// How far a chain of table transfers is followed before the answer is "??".
// Nothing in the description bounds one, since a table may reach itself, and a
// listing that walks a kilobyte before rendering a line is no use to the caller
// even where it terminates. (On the Z80 the unbounded chain is a run of `0xdd`.)
inline constexpr std::size_t max_instruction_bytes = 8;

// Renders the instruction at `address` as text, and says how many bytes it
// occupies. `byte_at(n)` is the nth byte of the instruction, counting from
// `address`; `address` itself is needed because a relative jump renders where
// it lands rather than how far it goes.
[[nodiscard]] inline Disassembly disassemble(const Description &description, const std::uint16_t address,
    const std::function_ref<std::uint8_t(std::size_t)> byte_at) {
  // Follow prefixes until a row that renders something is reached. An encoding
  // may take its displacement between the prefix and the byte that says what to
  // do (the Z80's `dd cb d op`), so the latch is filled inside this loop rather
  // than after it.
  std::size_t offset = 0;
  auto table = description.entry;
  const Row *row = nullptr;
  std::optional<std::uint8_t> latch;
  // The view a prefix chose, carried for the same reason the interpreter
  // carries it: the row is decoded under it and the text depends on it.
  std::uint8_t view = 0;
  std::uint8_t opcode = 0;
  while (true) {
    opcode = byte_at(offset);
    row = description.row_for(table, opcode);
    ++offset;
    if (!row)
      return {"??", offset};
    // Read here, inside the loop, because the displacement belongs to the
    // prefix row that declares `d`, and the loop moves on to the next table
    // before the row that uses it is reached.
    if (row->reads_displacement)
      latch = byte_at(offset++);
    const auto next = transfers_to(*row);
    if (!next)
      break;
    // A run of prefixes may be unbounded, and a disassembler has to answer, so
    // it gives up rather than follow one to the end of memory.
    if (offset >= max_instruction_bytes)
      return {"??", offset};
    view = row->steps[0].forwards_view ? view : row->steps[0].target_view;
    table = *next;
  }

  // The table a row was *decoded in* owns the renaming, which is why this asks
  // the table index rather than `row->table`: an inherited row renders under
  // the rules of whoever inherited it.
  const auto &rules = description.rules_for(table);
  // The displacement precedes any immediate, so it is taken before the pieces
  // are walked and whatever they read follows it, unless a prefix already did.
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
        // instruction: a relative jump never carries anything else. The sum is
        // formed at the width the machine forms it at.
        const auto to = static_cast<std::int8_t>(byte_at(offset));
        offset += 1;
        const auto end_of_instruction = static_cast<std::uint16_t>(address + offset);
        result += std::format("0x{:04x}", static_cast<std::uint16_t>(end_of_instruction + to));
        break;
      }
      case Piece::Kind::Imm16:
        result += std::format("0x{:04x}", static_cast<std::uint16_t>(byte_at(offset) | byte_at(offset + 1) << 8));
        offset += 2;
        break;
    }
  };

  const Resolution at{.vocabularies = description.vocabularies,
      .matched = row->matched,
      .rules = rules,
      .opcode = opcode,
      .view = view};
  for (const auto &part: row->pieces) {
    if (part.kind != Piece::Kind::Vocabulary) {
      render(part);
      continue;
    }
    // A member renders itself, because an indexed mode writes its displacement
    // in the middle of its own text.
    for (const auto &inner: member_of(at, part.reference).pieces)
      render(inner);
  }
  return {result, offset};
}

} // namespace specbolt::refract
