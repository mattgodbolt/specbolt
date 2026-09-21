#pragma once

// What a row means once an opcode has chosen among its vocabularies: which
// operands it resolves to, which opcodes it claims, and the checks that answer
// from those sets: precedence, reachability, totality.

#include <algorithm>
#include <array>
#include <bitset>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <vector>
#include "refract/Model.hpp"
#include "refract/Pattern.hpp"
#include "refract/TableError.hpp"

namespace specbolt::refract {

// What this opcode, decoded here, is displaced through, or nothing if it is not.
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

// Every vocabulary member the row names must be live: a `-` member is a hole,
// so the row does not cover that opcode even though the bits fit.
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

// Every opcode of mine is also one of theirs: an override, rather than an accident.
[[nodiscard]] constexpr bool within(const OpcodeSet &mine, const OpcodeSet &theirs) { return (mine & ~theirs).none(); }
[[nodiscard]] constexpr bool overlaps(const OpcodeSet &mine, const OpcodeSet &theirs) { return (mine & theirs).any(); }

// A pattern *generates* its opcodes, walking the cartesian product of its
// variable vocabularies and placing each combination, rather than being tested
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
    if (mine.none())
      throw table_error(rows[earlier].line, "this row matches no opcode at all");
    auto &already = claimed[rows[earlier].table];
    if (within(mine, already))
      throw table_error(rows[earlier].line, "an earlier row shadows this one completely");
    already |= mine;
    for (std::size_t later = earlier + 1; later < rows.size(); ++later) {
      if (rows[later].table != rows[earlier].table)
        continue;
      if (const auto &theirs = covers[later]; overlaps(mine, theirs) && !within(mine, theirs))
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

// Which tables are entered with a displacement already read. Where an encoding
// puts a byte before the opcode that decides what to do with it (the Z80's
// `dd cb d op`), the row that meets that byte reads it and hands it on. Derived
// from the gotos that reach a table rather than declared on it. A latched
// table's rows use the displacement instead of reading one, and its opcode
// arrives by an operand read rather than an instruction fetch.
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

// Every opcode of every table must decode to something. On real hardware one
// always does, since an unassigned encoding still has an effect, so a table that
// declines to say is an incomplete description rather than a permissive one. A
// catch-all row is how a table says "and everything else does this".
//
// Requiring it here is what lets the dispatch loop call without checking.
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
  // parentheses, as in the Z80's `reg.(hl) -> {index_mem:view}`. An operand
  // keeps the name and the indirection apart, so compare both halves rather
  // than the text.
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

// A derived table's own row that overlaps a row it inherits must be wholly
// contained in it. A derived row wins over everything inherited, so one that
// only partly overlaps an inherited row silently takes opcodes that row meant
// to keep. Precedence within one table is `check_row_precedence`; this relates
// a derived table's rows to the ones its parent decodes, whether the parent
// wrote them or inherited them in turn.
constexpr bool check_derived_rows_override(const Description &description, const std::span<const OpcodeSet> covers) {
  const auto rows = description.rows;
  const auto tables = description.tables;
  for (std::size_t mine = 0; mine < rows.size(); ++mine) {
    const auto &table = tables[rows[mine].table];
    if (!table.derived)
      continue;
    // The rows the parent decodes to, which is what this table inherits.
    std::vector<bool> inherited(rows.size());
    for (const auto &decoded: description.decoded[table.parent])
      if (decoded)
        inherited[*decoded] = true;
    for (std::size_t theirs = 0; theirs < rows.size(); ++theirs)
      if (inherited[theirs] && overlaps(covers[mine], covers[theirs]) && !within(covers[mine], covers[theirs]))
        throw table_error(rows[mine].line,
            "this row overlaps one it inherits from '" + std::string(tables[table.parent].name) +
                "' without replacing it or fitting inside it, so it takes opcodes that row meant to keep");
  }
  return true;
}

// Does this row's mnemonic render a displacement? It may say so itself, as an
// override row spelling a displaced operand out in full does, or through a
// vocabulary member that a view renamed to one.
[[nodiscard]] constexpr bool renders_displacement(
    const std::span<const Vocabulary> vocabularies, const Row &row, const std::uint8_t opcode, const Rules &rules) {
  return std::ranges::any_of(row.pieces, [&](const Piece &piece) {
    if (piece.kind == Piece::Kind::Displacement)
      return true;
    if (piece.kind != Piece::Kind::Vocabulary)
      return false;
    const auto member = member_of(
        {.vocabularies = vocabularies, .matched = row.matched, .rules = rules, .opcode = opcode}, piece.reference);
    return std::ranges::any_of(
        member.pieces, [](const Piece &inner) { return inner.kind == Piece::Kind::Displacement; });
  });
}

// `check_immediates` cross-checks the three columns about `n`; this is the same
// question for a displacement. It needs an opcode, since whether a row is
// displaced depends on which vocabulary member the opcode picks, and on the
// renaming the table it was decoded in applies, so it belongs here rather than
// beside the row.
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

// A table nothing reaches is a typo: nothing can ever decode in it, so nothing
// in it is ever exercised. Reachable *from the entry table*, not merely named
// by some goto: two tables that only reach each other are as dead as one
// nothing names. The walk is over the decoded tables rather than the rows so
// that a derived table reaches wherever its parent's rows go.
constexpr bool check_tables_used(const Description &description) {
  const auto tables = description.tables;
  for (const auto [which, table]: std::views::enumerate(tables))
    // A derived table with no rows of its own is its parent, renamed, which is
    // the whole point of one.
    if (!table.derived && !std::ranges::contains(description.rows, static_cast<std::uint8_t>(which), &Row::table))
      throw table_error(table.line, "this table has no rows");

  std::vector<bool> reachable(tables.size());
  std::vector<std::uint8_t> pending;
  const auto reach = [&](const std::uint8_t table) {
    if (!reachable[table]) {
      reachable[table] = true;
      pending.push_back(table);
    }
  };
  reach(description.entry);
  while (!pending.empty()) {
    const auto from = pending.back();
    pending.pop_back();
    // Totality is checked separately and after this, so an opcode that decodes
    // to nothing is somebody else's diagnostic rather than a reason to stop.
    for (const auto opcode: std::views::iota(0uz, 256uz))
      if (const auto *row = description.row_for(from, static_cast<std::uint8_t>(opcode)))
        for (const auto &step: row->steps)
          if (step.kind == Step::Kind::Goto)
            reach(step.target);
  }
  for (const auto [which, table]: std::views::enumerate(tables))
    if (!reachable[static_cast<std::size_t>(which)])
      throw table_error(table.line, "no goto reaches this table, so nothing in it is ever exercised");
  return true;
}

} // namespace specbolt::refract
