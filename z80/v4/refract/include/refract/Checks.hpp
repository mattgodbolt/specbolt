#pragma once

// The rules a whole description must obey, each asked once decoding is known: precedence between rows, totality of
// every table, reachability of every table, no inherited row spelling out a name its table renames, and a mnemonic
// rendering a displacement exactly when an operand is displaced. Each throws against a line, and `Compiled` runs them
// all when it is instantiated.

#include "refract/Decode.hpp"
#include "refract/Model.hpp"
#include "refract/TableError.hpp"

#include <algorithm>
#include <ranges>
#include <span>
#include <string>
#include <vector>

namespace specbolt::refract {

// Checks that every row wins some opcode, and that where two rows of one table
// overlap the earlier is wholly contained in the later. Line order silently
// decides who wins, so this says what the legal shapes are: containment is an
// override, and a partial overlap is an accident.
constexpr void check_row_precedence(const Description &description, const std::span<const OpcodeSet> covers) {
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
}

// Checks that every opcode of every table decodes to something. On real
// hardware one always does, since an unassigned encoding still has an effect,
// so a table that declines to say is an incomplete description rather than a
// permissive one. A catch-all row is how a table says "and everything else
// does this".
//
// Requiring it here is what lets the dispatch loop call without checking.
constexpr void check_tables_total(const Description &description) {
  for (std::size_t table = 0; table < description.tables.size(); ++table)
    for (std::size_t opcode = 0; opcode < 256; ++opcode)
      if (!description.row_for(static_cast<std::uint8_t>(table), static_cast<std::uint8_t>(opcode)))
        throw table_error(description.tables[table].line, "table '" + std::string(description.tables[table].name) +
                                                              "' does not say what opcode " + decimal(opcode) +
                                                              " does; add a row, or `xxxxxxxx` last to catch the rest");
}

// Whether any step of this row names `what` as a literal, where a rule cannot
// reach it.
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

// Checks that no inherited row spells out, as a literal, a name the table
// inheriting it renames. A rule rewrites vocabulary references and never
// literal text, which is what lets a row that means what it says mean it. The
// same silence hides a mistake: a row spelling a renamed name out, inherited
// unchanged by the table that renames it, is almost certainly wrong.
//
// A row written *in* the derived table is exempt: putting it there is how one
// says the literal was meant.
constexpr void check_inherited_literals(const Description &description) {
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
}

// Checks that a derived table's own row overlapping a row it inherits is wholly
// contained in it. A derived row wins over everything inherited, so one that
// only partly overlaps an inherited row silently takes opcodes that row meant
// to keep. Precedence within one table is `check_row_precedence`; this relates
// a derived table's rows to the ones its parent decodes, whether the parent
// wrote them or inherited them in turn.
constexpr void check_derived_rows_override(const Description &description, const std::span<const OpcodeSet> covers) {
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
}

// Whether this row's mnemonic renders a displacement at this opcode. It may say
// so itself, as an override row spelling a displaced operand out in full does,
// or through a vocabulary member that a view renamed to one.
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

// Checks that a row's mnemonic renders a displacement exactly when one of its
// operands is displaced. `check_immediates` asks the same about `n`; this one
// needs an opcode, since whether a row is displaced depends on which vocabulary
// member the opcode picks, and on the renaming the table it was decoded in
// applies, so it belongs here rather than beside the row.
//
// A mismatch is not a length error: both the interpreter and the disassembler
// take the length from `displaced_through`, so they agree about how many bytes
// to read and disagree only about what to print. The disassembler would quietly
// name an addressing mode the machine did not use, or omit the one it did.
constexpr void check_displacement_rendered(const Description &description) {
  const auto vocabularies = description.vocabularies;
  for (const auto &[table, opcode, row, rules]: instructions_of(description)) {
    const auto displaced = displaced_through(vocabularies, *row, opcode, *rules).has_value();
    if (displaced != renders_displacement(vocabularies, *row, opcode, *rules))
      throw table_error(row->line, displaced
                                       ? "this row is displaced but its mnemonic does not say so; write `+d` where the "
                                         "displacement belongs"
                                       : "this row's mnemonic renders a displacement that no operand of it uses");
  }
}

// Checks that every table not derived has rows of its own, and that every table
// is reachable from the entry table by some chain of gotos. A table nothing
// reaches is a typo: nothing can ever decode in it, so nothing in it is ever
// exercised. Reachable *from the entry table*, not merely named by some goto:
// two tables that only reach each other are as dead as one nothing names. The
// walk is over the decoded tables rather than the rows so that a derived table
// reaches wherever its parent's rows go.
constexpr void check_tables_used(const Description &description) {
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
}

} // namespace specbolt::refract
