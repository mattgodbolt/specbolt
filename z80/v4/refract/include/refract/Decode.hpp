#pragma once

// Decoding: which row each opcode of each table decodes to, and what an instruction so decoded is. A row claims the
// opcodes its pattern matches whose vocabulary members are all live; earlier rows win, and a derived table inherits
// whatever it does not claim itself. Everything downstream, the disassembler and the interpreter alike, is built on
// the answers here, and the whole-description rules that need them are in Checks.hpp.

#include "refract/Model.hpp"
#include "refract/Pattern.hpp"
#include "refract/TableError.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

namespace specbolt::refract {

// The operand this opcode, decoded in this row under these rules, is displaced
// through, or nothing if it is not displaced at all.
// Nothing declares this: a row names a vocabulary member, a view says that
// member is now a displaced one, and the answer is whatever the operands
// resolve to. One per instruction, not one per operand: an instruction that
// reads and writes through the same address wants one displacement read and
// one sum formed, as the chip does (the Z80's `inc (ix+d)` is one).
//
// This is the one source of an instruction's length: Disassemble.hpp and
// Execute.hpp both ask it, which is why the two agree about how many bytes an
// instruction has.
//
// No `view` parameter: every member of a vocabulary a view selects is required
// to have the same shape, so view 0 answers for all of them. That requirement
// is `check_view_vocabulary` in Parse.hpp, without which one page of a
// prefixed encoding would quietly get another's addressing mode.
[[nodiscard]] constexpr std::optional<Resolved> displaced_through(
    const std::span<const Vocabulary> vocabularies, const Row &row, const std::uint8_t opcode, const Rules &rules) {
  const Resolution at{.vocabularies = vocabularies, .matched = row.matched, .rules = rules, .opcode = opcode};
  std::optional<Resolved> found;
  const auto consider = [&](const Operand &operand) {
    const auto resolved = resolve(at, operand);
    if (!resolved.displaced)
      return;
    if (found && found->name != resolved.name)
      throw table_error(row.line, "an instruction may only be displaced through one base");
    found = resolved;
  };
  for (const auto &step: row.steps) {
    for (const auto &operand: step.operands)
      consider(operand);
    for (const auto &operand: step.destinations)
      consider(operand);
  }
  return found;
}

// Whether every vocabulary member this row names at this opcode is live. A `-`
// member is a hole, so a row naming one does not cover that opcode even though
// the bits fit.
[[nodiscard]] constexpr bool members_live(
    const std::span<const Vocabulary> vocabularies, const Row &row, const std::uint8_t opcode) {
  const Resolution at{.vocabularies = vocabularies, .matched = row.matched, .opcode = opcode};
  const auto live = [&](const Reference reference) { return !member_of(at, reference).hole; };
  const auto operands_live = [&](const auto &operands) {
    return std::ranges::all_of(operands,
        [&](const Operand &operand) { return operand.kind != Operand::Kind::Vocabulary || live(operand.reference); });
  };
  return std::ranges::all_of(row.pieces, [&](const Piece &piece) {
    return piece.kind != Piece::Kind::Vocabulary || live(piece.reference);
  }) && std::ranges::all_of(row.steps, [&](const Step &step) {
    return operands_live(step.operands) && operands_live(step.destinations) &&
           (!step.operation_reference || live(*step.operation_reference));
  });
}

// A set of opcodes, as bits, so containment and overlap are whole-set
// operations. `std::bitset` has been usable in constant evaluation since C++23.
using OpcodeSet = std::bitset<256>;

// Whether every opcode of `mine` is also one of `theirs`: an override, rather
// than an accident.
[[nodiscard]] constexpr bool within(const OpcodeSet &mine, const OpcodeSet &theirs) { return (mine & ~theirs).none(); }
// Whether the two sets share any opcode at all.
[[nodiscard]] constexpr bool overlaps(const OpcodeSet &mine, const OpcodeSet &theirs) { return (mine & theirs).any(); }

// The opcodes a row claims: every opcode its pattern matches whose vocabulary
// members are all live. A pattern *generates* them, walking the cartesian
// product of its slices and placing each combination, rather than being tested
// against all 256. `BitSlice::place` exists for exactly this.
[[nodiscard]] constexpr OpcodeSet opcodes_of(const std::span<const Vocabulary> vocabularies, const Row &row) {
  OpcodeSet result;
  std::size_t combinations = 1;
  for (const auto &slice: row.matched.slices)
    combinations *= std::size_t{slice.mask} + 1;
  for (std::size_t at = 0; at < combinations; ++at) {
    auto opcode = row.matched.opcode_bits;
    auto remaining = at;
    for (const auto &slice: row.matched.slices) {
      const auto values = std::size_t{slice.mask} + 1;
      opcode = static_cast<std::uint8_t>(opcode | slice.place(static_cast<std::uint8_t>(remaining % values)));
      remaining /= values;
    }
    if (members_live(vocabularies, row, opcode))
      result.set(opcode);
  }
  return result;
}

// The opcodes each row claims, index-coupled to `rows`. Walking a row's
// cartesian product is the expensive part of evaluating a description, and
// three of the checks below want the answer, so it is computed once here and
// passed to each.
[[nodiscard]] constexpr std::vector<OpcodeSet> opcodes_of_each(
    const std::span<const Vocabulary> vocabularies, const std::span<const Row> rows) {
  return rows | std::views::transform([&](const Row &row) { return opcodes_of(vocabularies, row); }) |
         std::ranges::to<std::vector>();
}

// One decoded instruction: a (table, opcode) that a row answers to, and the
// renaming it answers under. The checks below are each one question asked of
// every one of these, and walking is not what any of them is about.
struct Instruction {
  std::uint8_t table{};
  std::uint8_t opcode{};
  const Row *row{};
  const Rules *rules{};
};

// Every (table, opcode) the description decodes to a row, each with that row
// and the rules the table reads it under.
[[nodiscard]] constexpr std::vector<Instruction> instructions_of(const Description &description) {
  std::vector<Instruction> all;
  // Every table is total, so this is the exact size.
  all.reserve(description.tables.size() * 256);
  for (std::size_t table = 0; table < description.tables.size(); ++table)
    for (std::size_t opcode = 0; opcode < 256; ++opcode) {
      const auto at = static_cast<std::uint8_t>(table);
      const auto byte = static_cast<std::uint8_t>(opcode);
      if (const auto *row = description.row_for(at, byte))
        all.push_back({at, byte, row, &description.rules_for(at)});
    }
  return all;
}

// Builds each table's decode table: which row, as an index into `rows`, each of
// its 256 opcodes decodes to. Earlier rows win; then a derived table takes from
// its parent whatever it did not claim itself. Declaration order resolves a
// chain, because a parent is always declared before its children.
[[nodiscard]] constexpr std::vector<DecodeTable> decode_tables(const std::span<const Row> rows,
    const std::span<const OpcodeSet> opcodes, const std::span<const TableDecl> tables) {
  std::vector<DecodeTable> all(tables.size());
  // `rows` and `opcodes` are index-coupled by construction, because
  // `opcodes_of_each` built one from the other, so zip says that rather than
  // trusting it.
  for (const auto [index, row, claimed]: std::views::zip(std::views::iota(0uz), rows, opcodes))
    for (const auto opcode: std::views::iota(0uz, 256uz))
      if (claimed.test(opcode) && !all[row.table][opcode])
        all[row.table][opcode] = index;
  for (std::size_t which = 0; which < tables.size(); ++which)
    if (tables[which].derived)
      for (std::size_t opcode = 0; opcode < 256; ++opcode)
        if (!all[which][opcode])
          all[which][opcode] = all[tables[which].parent][opcode];
  return all;
}

// Per table, whether it is entered with a displacement already read. Where an
// encoding puts a byte before the opcode that decides what to do with it (the
// Z80's `dd cb d op`), the row that meets that byte reads it and hands it on.
// Derived from the gotos that reach a table rather than declared on it. A
// latched table's rows use the displacement instead of reading one, and its
// opcode arrives by an operand read rather than an instruction fetch.
[[nodiscard]] constexpr std::vector<bool> latched_tables(
    const std::span<const Row> rows, const std::size_t num_tables) {
  // Empty until some goto has said, so that "not reached yet" and "reached
  // without a displacement" stay different answers rather than both being false.
  std::vector<std::optional<bool>> reached(num_tables);
  for (const auto &row: rows)
    for (const auto &step: row.steps) {
      if (step.kind != Step::Kind::Goto)
        continue;
      auto &latched = reached[step.target];
      if (latched && *latched != row.reads_displacement)
        throw table_error(row.line, "this table is reached both with and without a displacement");
      latched = row.reads_displacement;
    }
  return reached | std::views::transform([](const std::optional<bool> latched) { return latched.value_or(false); }) |
         std::ranges::to<std::vector>();
}

} // namespace specbolt::refract
