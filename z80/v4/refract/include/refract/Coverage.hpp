#pragma once

// What a row means once an opcode has chosen among its vocabularies: which
// operands it resolves to, which opcodes it claims, and the checks that answer
// from those sets -- precedence, reachability, totality.

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <vector>
#include "refract/Model.hpp"
#include "refract/Pattern.hpp"
#include "refract/TableError.hpp"

namespace specbolt::refract {

// What this opcode, decoded here, is displaced through -- nothing if it is not.
// Nothing declares this: a row says `{reg:z}`, a view says that member is now
// `(ix+d)`, and the answer is whatever the operands resolve to.
//
// One per instruction, not one per operand. `inc (ix+d)` reads and writes
// through the same address, and the chip reads one displacement and forms one
// sum; forming it per operand would pay for it twice.
[[nodiscard]] constexpr std::optional<Operand> displaced_through(
    const std::span<const Vocabulary> vocabularies, const Row &row, const std::uint8_t opcode, const Rules &rules) {
  std::optional<Operand> found;
  const auto consider = [&](const Operand &operand) {
    const auto resolved = resolve(vocabularies, operand, row.matched, opcode, rules);
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

// Every vocabulary member the row names must be live: a `-` member is a hole,
// so the row does not cover that opcode even though the bits fit.
[[nodiscard]] constexpr bool members_live(
    const std::span<const Vocabulary> vocabularies, const Row &row, const std::uint8_t opcode) {
  const auto live = [&](const Reference reference) {
    return !member_of(vocabularies, reference, row.matched, opcode).hole;
  };
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

// Earlier rows win, so a specific encoding must precede the general one that
// would otherwise swallow it: `halt` before `ld {reg:y}, {reg:z}`.
// A set of opcodes, as bits, so containment and overlap are four operations
// rather than 256.
struct OpcodeSet {
  std::array<std::uint64_t, 4> words{};

  constexpr void add(const std::uint8_t opcode) { words[opcode >> 6] |= std::uint64_t{1} << (opcode & 63); }
  [[nodiscard]] constexpr bool contains(const std::uint8_t opcode) const {
    return (words[opcode >> 6] >> (opcode & 63) & 1) != 0;
  }
  [[nodiscard]] constexpr bool empty() const {
    return std::ranges::all_of(words, [](const std::uint64_t word) { return word == 0; });
  }
  constexpr void add_all(const OpcodeSet &other) {
    for (std::size_t at = 0; at < words.size(); ++at)
      words[at] |= other.words[at];
  }
  [[nodiscard]] constexpr bool overlaps(const OpcodeSet &other) const {
    for (std::size_t at = 0; at < words.size(); ++at)
      if ((words[at] & other.words[at]) != 0)
        return true;
    return false;
  }
  // Every opcode of mine is also one of theirs: an override, rather than an accident.
  [[nodiscard]] constexpr bool within(const OpcodeSet &other) const {
    for (std::size_t at = 0; at < words.size(); ++at)
      if ((words[at] & ~other.words[at]) != 0)
        return false;
    return true;
  }
};

// A pattern *generates* its opcodes -- walk the cartesian product of its
// variable vocabularies and place each combination -- rather than being tested
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
      result.add(opcode);
  }
  return result;
}

// Walking a row's cartesian product is the expensive part of evaluating a
// description, and three of the checks below want the answer, so it is computed
// once and passed to each.
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

[[nodiscard]] constexpr std::vector<Instruction> instructions_of(const Description &description) {
  std::vector<Instruction> all;
  // Every table is total, so this is the exact size rather than a guess. Worth
  // saying: growing it instead costs a second of constant evaluation.
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

// Line order silently decides who wins, so say what the legal shapes are: a row
// must win something, and where two rows overlap the earlier must be wholly
// contained in the later. That is an override. A partial overlap is an accident.
constexpr bool check_row_precedence(const Description &description, const std::span<const OpcodeSet> covers) {
  const auto rows = description.rows;
  // What each row wins once the rows before it have taken their share.
  // Precedence is a fact about opcode sets, not about vocabularies, so this
  // never resolves a name.
  std::vector<OpcodeSet> claimed(description.tables.size());

  for (std::size_t earlier = 0; earlier < rows.size(); ++earlier) {
    const auto &mine = covers[earlier];
    if (mine.empty())
      throw table_error(rows[earlier].line, "this row matches no opcode at all");
    auto &already = claimed[rows[earlier].table];
    if (mine.within(already))
      throw table_error(rows[earlier].line, "an earlier row shadows this one completely");
    already.add_all(mine);
    for (std::size_t later = earlier + 1; later < rows.size(); ++later) {
      if (rows[later].table != rows[earlier].table)
        continue;
      if (const auto &theirs = covers[later]; mine.overlaps(theirs) && !mine.within(theirs))
        throw table_error(rows[earlier].line, "this row overlaps a later one without being contained by it");
    }
  }
  return true;
}

// Earlier rows win; then a derived table takes from its parent whatever it did
// not claim itself. Declaration order resolves a chain, because a parent is
// always declared before its children.
[[nodiscard]] constexpr std::vector<DecodeTable> decode_tables(const std::span<const Row> rows,
    const std::span<const OpcodeSet> opcodes, const std::span<const TableDecl> tables) {
  std::vector<DecodeTable> all(tables.size());
  for (std::size_t index = 0; index < rows.size(); ++index)
    for (std::size_t opcode = 0; opcode < 256; ++opcode)
      if (opcodes[index].contains(static_cast<std::uint8_t>(opcode)) && !all[rows[index].table][opcode])
        all[rows[index].table][opcode] = index;
  for (std::size_t which = 0; which < tables.size(); ++which)
    if (tables[which].derived)
      for (std::size_t opcode = 0; opcode < 256; ++opcode)
        if (!all[which][opcode])
          all[which][opcode] = all[tables[which].parent][opcode];
  return all;
}

// Which tables are entered with a displacement already read. Where an encoding
// interleaves a byte before the opcode that decides what to do with it -- the
// Z80's `dd cb d op`, for example -- the row that meets that byte reads it and
// hands it on rather than using it. Two consequences, both derived from the
// gotos that reach a table rather than declared on it: its rows use the
// displacement instead of reading one, and its opcode arrives by an operand
// read rather than an instruction fetch -- the machine has already committed,
// which is why the real chip does not increment R for that byte.
[[nodiscard]] constexpr std::vector<bool> latched_tables(
    const std::span<const Row> rows, const std::size_t num_tables) {
  std::vector<bool> latched(num_tables);
  std::vector<bool> seen(num_tables);
  for (const auto &row: rows)
    for (const auto &step: row.steps) {
      if (step.kind != Step::Kind::Goto)
        continue;
      if (seen[step.target] && latched[step.target] != row.reads_displacement)
        throw table_error(row.line, "this table is reached both with and without a displacement");
      seen[step.target] = true;
      latched[step.target] = row.reads_displacement;
    }
  return latched;
}

// Every opcode of every table must decode to something. On real hardware one
// always does -- an unassigned encoding still has an effect -- so a table that
// declines to say is an incomplete description rather than a permissive one. A
// catch-all row is how a table says "and everything else does this".
//
// Requiring it here is what lets the dispatch loop call without checking.
// The one check about absence, so the only one that cannot walk `instructions_of`.
constexpr bool check_tables_total(const Description &description) {
  for (std::size_t table = 0; table < description.tables.size(); ++table)
    for (std::size_t opcode = 0; opcode < 256; ++opcode)
      if (!description.row_for(static_cast<std::uint8_t>(table), static_cast<std::uint8_t>(opcode)))
        throw table_error(description.tables[table].line, "table '" + std::string(description.tables[table].name) +
                                                              "' does not say what opcode " + decimal(opcode) +
                                                              " does; add a row, or `xxxxxxxx` last to catch the rest");
  return true;
}

// Does any step of this row name `what` as a literal, where a rule cannot reach
// it?
[[nodiscard]] constexpr bool names_literally(const Row &row, std::string_view what) {
  // A rule's left side is written as the vocabulary writes it, so it may carry
  // parentheses -- `reg.(hl) -> (ix+d)`. An operand keeps the name and the
  // indirection apart, so compare both halves rather than the text.
  auto indirect = false;
  if (what.starts_with('(') && what.ends_with(')')) {
    indirect = true;
    what = what.substr(1, what.size() - 2);
  }
  const auto matches = [what, indirect](const Operand &operand) {
    return operand.kind == Operand::Kind::Named && operand.indirect == indirect && operand.name.view() == what;
  };
  return std::ranges::any_of(row.steps, [&](const Step &step) {
    return std::ranges::any_of(step.operands, matches) || std::ranges::any_of(step.destinations, matches);
  });
}

// A rule rewrites vocabulary references and never literal text, which is what
// lets a row that means what it says mean it. The same silence hides a mistake:
// a row spelling a renamed name out, inherited unchanged by the table that
// renames it, is almost certainly wrong.
//
// A row written *in* the derived table is exempt: putting it there is how one
// says the literal was meant.
constexpr bool check_inherited_literals(const Description &description) {
  for (const auto &[table, opcode, row, rules]: instructions_of(description)) {
    if (row->table == table) // its own row, so the literal was meant
      continue;
    for (const auto &rule: *rules)
      if (names_literally(*row, rule.from))
        throw table_error(row->line,
            "table '" + std::string(description.tables[table].name) + "' renames '" + std::string(rule.from) +
                "', and this row names it literally where a rule cannot reach it; give that table its own row, "
                "or name a vocabulary");
  }
  return true;
}

// Precedence within a table is checked pairwise, and a derived table's own rows
// win over everything it inherits -- but nothing yet relates the two. A derived
// row that overlaps a parent row without being contained in it is silently
// taking opcodes the parent meant to keep.
//
// This is the check that would have caught writing `{reg:y}` for `{real:y}` in the
// `ix` table: `r` has no hole at slot 6, so the row would claim `0x76` and
// `halt` would quietly vanish from the prefixed pages.
constexpr bool check_derived_rows_override(const Description &description, const std::span<const OpcodeSet> covers) {
  const auto rows = description.rows;
  const auto tables = description.tables;
  for (std::size_t mine = 0; mine < rows.size(); ++mine) {
    const auto &table = tables[rows[mine].table];
    if (!table.derived)
      continue;
    for (std::size_t theirs = 0; theirs < rows.size(); ++theirs)
      if (rows[theirs].table == table.parent && covers[mine].overlaps(covers[theirs]) &&
          !covers[mine].within(covers[theirs]))
        throw table_error(rows[mine].line,
            "this row overlaps one it inherits from '" + std::string(tables[table.parent].name) +
                "' without replacing it or fitting inside it, so it takes opcodes that row meant to keep");
  }
  return true;
}

// Does this row's mnemonic render a displacement? It may say so itself -- an
// override row writing `(ix+d)` in full -- or through a vocabulary member that
// a view renamed to one.
[[nodiscard]] constexpr bool renders_displacement(
    const std::span<const Vocabulary> vocabularies, const Row &row, const std::uint8_t opcode, const Rules &rules) {
  return std::ranges::any_of(row.pieces, [&](const Piece &piece) {
    if (piece.kind == Piece::Kind::Displacement)
      return true;
    if (piece.kind != Piece::Kind::Vocabulary)
      return false;
    const auto member = member_of(vocabularies, piece.reference, row.matched, opcode, rules);
    return std::ranges::any_of(
        member.pieces, [](const Piece &inner) { return inner.kind == Piece::Kind::Displacement; });
  });
}

// `check_immediates` cross-checks the three columns about `n`; this is the same
// question for a displacement. It needs an opcode -- whether a row is displaced
// depends on which vocabulary member the opcode picks, and on the renaming of
// the table it was decoded in -- so it belongs here rather than beside the row.
//
// A mismatch is not a length error: both the interpreter and the disassembler
// take the length from `displaced_through`, so they agree about how many bytes
// to read and disagree only about what to print. The disassembler would quietly
// name an addressing mode the machine did not use, or omit the one it did.
constexpr bool check_displacement_rendered(const Description &description) {
  const auto vocabularies = description.vocabularies;
  for (const auto &[table, opcode, row, rules]: instructions_of(description)) {
    const auto displaced = displaced_through(vocabularies, *row, opcode, *rules).has_value();
    if (displaced != renders_displacement(vocabularies, *row, opcode, *rules))
      throw table_error(row->line, displaced
                                       ? "this row is displaced but its mnemonic does not say so; write `+d` where the "
                                         "displacement belongs"
                                       : "this row's mnemonic renders a displacement that no operand of it uses");
  }
  return true;
}

// A table nothing reaches is a typo: nothing can ever decode in it. It is still
// generated -- every table's dispatch is instantiated regardless of whether a
// goto names it -- so this catches the mistake rather than un-checked code.
constexpr bool check_tables_used(const Description &description) {
  const auto rows = description.rows;
  const auto tables = description.tables;
  for (std::size_t which = 0; which < tables.size(); ++which) {
    // A derived table with no rows of its own is its parent, renamed -- which is
    // the whole point of one.
    if (!tables[which].derived && std::ranges::none_of(rows, [&](const Row &row) { return row.table == which; }))
      throw table_error(tables[which].line, "this table has no rows");
    if (which == description.entry)
      continue;
    const auto reached = std::ranges::any_of(rows, [&](const Row &row) {
      return std::ranges::any_of(
          row.steps, [&](const Step &step) { return step.kind == Step::Kind::Goto && step.target == which; });
    });
    if (!reached)
      throw table_error(tables[which].line, "no goto reaches this table, so nothing in it is ever checked");
  }
  return true;
}

} // namespace specbolt::refract
