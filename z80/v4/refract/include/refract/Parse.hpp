#pragma once

// The three kinds of declaration a description contains, each reading the whole
// text and returning what it found. Each returns a `std::vector`: nothing here
// knows how many of anything a description holds, and nothing has to. See
// ToArray.hpp for where that becomes a size.

#include "refract/Lexical.hpp"
#include "refract/Model.hpp"
#include "refract/Parser.hpp"
#include "refract/Pattern.hpp"
#include "refract/TableError.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace specbolt::refract {

[[nodiscard]] constexpr std::vector<Vocabulary> parse_vocabularies(const std::string_view description) {
  std::vector<Vocabulary> result;
  for (const auto [at, text]: lines_of(description)) {
    if (!is_vocabulary(text))
      continue;
    Parser parser(text);
    parser.skip_word();
    Vocabulary vocabulary;
    vocabulary.line = at;
    vocabulary.name = parser.next_word();
    if (vocabulary.name.empty())
      throw table_error(at, "vocabulary declaration has no name");
    auto next = parser.next_word();
    // `vocab pair : R16 = ...`: the scope its members are looked up in, rather
    // than every location the CPU offers.
    if (next == ":") {
      vocabulary.scope = parser.next_word();
      if (vocabulary.scope.empty())
        throw table_error(at, "':' introduces the scope a vocabulary's members come from, and none was given");
      next = parser.next_word();
    }
    if (next != "=")
      throw table_error(at, "expected '=' in vocabulary declaration");
    while (!parser.eof()) {
      const auto value = parser.next_word();
      if (value.empty())
        continue;
      if (!vocabulary.members.try_push_back(parse_member(value, at)))
        throw table_error(at, "too many members in vocabulary");
    }
    if (vocabulary.members.empty())
      throw table_error(at, "vocabulary declares no members");
    if (std::ranges::contains(result, vocabulary.name, &Vocabulary::name))
      throw table_error(at, "duplicate vocabulary name");
    result.push_back(vocabulary);
  }
  return result;
}

// After blanks and comments, every line is a declaration or a row. A line that
// is neither is a mistyped one of them (a row that lost its separators, or
// `vocabularies` for `vocab`) and would otherwise be skipped in silence, surfacing
// much later as an opcode nothing decodes.
constexpr bool check_every_line_means_something(const std::string_view description) {
  for (const auto [at, text]: lines_of(description)) {
    if (text.empty() || text.front() == '#' || is_vocabulary(text) || is_table(text) || is_row(text))
      continue;
    throw table_error(at, "this is not a comment, a declaration, or a row; a row needs its '|' separators");
  }
  return true;
}

[[nodiscard]] constexpr std::optional<std::size_t> find_vocabulary(
    const std::span<const Vocabulary> vocabularies, const std::string_view name) {
  const auto found = std::ranges::find(vocabularies, name, &Vocabulary::name);
  if (found == vocabularies.end())
    return std::nullopt;
  return static_cast<std::size_t>(found - vocabularies.begin());
}

// `indexed(view:index)` declares a table called `indexed`; the parenthesised
// part is its view. Both the declaration and the scan for a table's rows need
// the bare name.
[[nodiscard]] constexpr std::string_view table_name_of(const std::string_view word) {
  const auto open = word.find('(');
  return open == std::string_view::npos ? word : word.substr(0, open);
}

[[nodiscard]] constexpr Reference reference_from_braces(std::span<const Vocabulary> vocabularies, std::string_view text,
    const Pattern &matched, std::size_t line, const TableDecl &table);

// `pair.hl->ix, reg.h -> ixh`: either spacing, because both read naturally.
// A right side of `{index:view}` substitutes whichever member the table's view
// selects, which is what lets one table stand for both ix and iy.
constexpr void parse_substitutions(const std::string_view text, const std::span<const Vocabulary> vocabularies,
    TableDecl &table, const std::size_t line) {
  Parser list(text);
  while (!list.eof()) {
    const auto rule = list.next_field(',');
    if (rule.empty())
      continue;
    const auto arrow = rule.find("->");
    if (arrow == std::string_view::npos)
      throw table_error(line, "expected '->' in table substitution '" + std::string(rule) + "'");
    const auto left = Parser::trim(rule.substr(0, arrow));
    const auto to = Parser::trim(rule.substr(arrow + 2));
    if (left.empty() || to.empty())
      throw table_error(line, "a table substitution needs a name on each side of '->'");
    const auto dot = left.find('.');
    if (dot == std::string_view::npos)
      throw table_error(line, "a table substitution names the vocabulary it rewrites, as in 'reg.h -> ixh'");
    const auto vocabulary = left.substr(0, dot);
    const auto named = find_vocabulary(vocabularies, vocabulary);
    if (!named)
      throw table_error(line, "substitution names a vocabulary that does not exist");
    const auto from = left.substr(dot + 1);
    if (from.empty())
      throw table_error(line, "a table substitution needs a name on each side of '->'");
    if (!std::ranges::contains(vocabularies[*named].members, from, &Member::display))
      throw table_error(line, "vocabulary '" + std::string(vocabulary) + "' has no member '" + std::string(from) + "'");
    Rule substitution{.vocabulary_index = static_cast<std::uint8_t>(*named), .from = from};
    if (to.starts_with('{')) {
      if (!table.takes_view())
        throw table_error(line, "only a table that takes a view may substitute a view reference");
      const auto reference = reference_from_braces(vocabularies, to, Pattern{}, line, table);
      if (!reference.from_view)
        throw table_error(line, "a substitution's reference must be selected by the table's view");
      substitution.to_is_view = true;
      substitution.to_vocabulary = reference.vocabulary_index;
    }
    else {
      // Coverage is worked out before any rule is applied, so a row renamed to
      // nothing would still claim its opcodes and then resolve to a default zero.
      substitution.to = parse_member(to, line);
      if (substitution.to.hole)
        throw table_error(line, "a substitution cannot rename something to nothing; a hole belongs in a vocabulary");
    }
    if (!table.rules.try_push_back(substitution))
      throw table_error(line, "too many substitutions in table");
  }
}

[[nodiscard]] constexpr std::vector<TableDecl> parse_tables(
    const std::string_view description, const std::span<const Vocabulary> vocabularies) {
  std::vector<TableDecl> result;
  for (const auto [at, text]: lines_of(description)) {
    if (!is_table(text))
      continue;
    Parser parser(text);
    parser.skip_word();
    auto name = parser.next_word();
    if (name.empty())
      throw table_error(at, "table declaration has no name");
    TableDecl table{.line = at};
    // `indexed(view:index)`: the table is decoded once per member of `index`,
    // and a row writes `view` where a slice letter would go.
    if (const auto open = name.find('('); open != std::string_view::npos) {
      if (!name.ends_with(')'))
        throw table_error(at, "unterminated '(' in table view");
      Parser inner(name.substr(open + 1, name.size() - open - 2));
      table.view_name = inner.take_until(':');
      const auto vocabulary = inner.rest();
      if (table.view_name.empty() || vocabulary.empty())
        throw table_error(at, "a table view names itself and a vocabulary, as in 'indexed(view:index)'");
      const auto named = find_vocabulary(vocabularies, vocabulary);
      if (!named)
        throw table_error(at, "table view names a vocabulary that does not exist");
      table.view_vocabulary = static_cast<std::uint8_t>(*named);
      name = name.substr(0, open);
    }
    table.name = name;
    if (std::ranges::contains(result, name, &TableDecl::name))
      throw table_error(at, "duplicate table name");
    if (const auto equals = parser.next_word(); !equals.empty()) {
      if (equals != "=")
        throw table_error(at, "expected '= <parent> with <substitutions>' after the table name");
      // Only a table already declared, which makes the derivation a forest: a
      // parent's own rows are resolved before anything inherits them. `result`
      // holds exactly those, this one not being in it yet.
      const auto parent = parser.next_word();
      const auto found = std::ranges::find(result, parent, &TableDecl::name);
      if (found == result.end())
        throw table_error(at, "no table named '" + std::string(parent) + "' is declared above this one");
      table.derived = true;
      table.parent = static_cast<std::uint8_t>(found - result.begin());
      if (parser.next_word() != "with")
        throw table_error(at, "expected 'with' after the parent table name");
      parse_substitutions(parser.rest(), vocabularies, table, at);
      if (table.rules.empty())
        throw table_error(at, "a derived table declares no substitutions, so it is its parent");
    }
    result.push_back(table);
  }
  return result;
}

[[nodiscard]] constexpr std::uint8_t find_table(
    const std::span<const TableDecl> tables, const std::string_view name, const std::size_t line) {
  const auto found = std::ranges::find(tables, name, &TableDecl::name);
  if (found == tables.end())
    throw table_error(line, "no table named '" + std::string(name) + "'");
  return static_cast<std::uint8_t>(found - tables.begin());
}

[[nodiscard]] constexpr std::optional<std::size_t> find_slice(const Pattern &matched, const char name) {
  const auto found = std::ranges::find(matched.slices, name, &BitSlice::name);
  if (found == matched.slices.end())
    return std::nullopt;
  return static_cast<std::size_t>(found - matched.slices.begin());
}

// A view is chosen by a prefix, long after everything about the instruction
// that a compile-time check can see has been settled. So every check resolves
// such a reference at member 0 and applies the answer to all of them --
// `displaced_through` does not even take a view, which is only sound if the
// members agree about everything except which location they name.
//
// Without this, `vocab index_mem = (ix+d)/delay=1 (iy)` compiles clean and the
// `fd` page silently runs one addressing mode while printing another. It is the
// one mistake in the format that would otherwise produce a wrong emulator
// rather than a line number.
//
// Reported against the declaration rather than the row that selects it, because
// that is the line to edit; the row is named in the message, since a vocabulary
// nothing selects by a view is free to hold whatever it likes.
constexpr void check_view_vocabulary(const Vocabulary &vocabulary, const std::size_t used_at) {
  const auto &first = vocabulary.members[0];
  const auto shape_of = [](const Member &member) {
    return std::tuple{member.hole, member.operand.indirect, member.operand.displaced, member.operand.write_back_delay,
        member.operand.kind, member.operation.empty(), member.arguments.size(), member.pieces.size()};
  };
  const auto complaint = [&](const Member &member, const std::string_view must) {
    return table_error(vocabulary.line, "vocabulary '" + std::string(vocabulary.name) + "' is selected by a view (at " +
                                            at_line(used_at) + "), so " + std::string(must) + "; '" +
                                            std::string(member.display) + "' does not match '" +
                                            std::string(first.display) + "'");
  };
  for (const auto &member: vocabulary.members) {
    if (shape_of(member) != shape_of(first))
      throw complaint(member, "all of its members must have the same shape");
    if (!std::ranges::equal(member.pieces, first.pieces, {}, &Piece::kind, &Piece::kind))
      throw complaint(member, "all of its members must render the same way");
  }
}

// `{reg:z}` binds the vocabulary `reg` to the slice `z`; in a table that takes
// one, `{index:view}` binds it to the view instead, which the opcode does not
// carry and a prefix chose.
[[nodiscard]] constexpr Reference parse_reference(const std::span<const Vocabulary> vocabularies,
    const std::string_view inner, const Pattern &matched, const std::size_t line, const TableDecl &table) {
  Parser parser(inner);
  const auto name = parser.take_until(':');
  const auto slice = parser.rest();
  if (name.empty() || slice.empty())
    throw table_error(line, "a reference names a vocabulary and one slice letter, as in {reg:z}");
  const auto field = find_vocabulary(vocabularies, name);
  if (!field)
    throw table_error(line, "reference names a vocabulary that does not exist");
  if (table.takes_view() && slice == table.view_name) {
    if (vocabularies[*field].members.size() != vocabularies[table.view_vocabulary].members.size())
      throw table_error(line, "vocabulary has a different number of members than the table's view");
    check_view_vocabulary(vocabularies[*field], line);
    return {.vocabulary_index = static_cast<std::uint8_t>(*field), .from_view = true};
  }
  if (slice.size() != 1)
    throw table_error(line, "a reference names a vocabulary and one slice letter, as in {reg:z}");
  const auto found = find_slice(matched, slice.front());
  if (!found)
    throw table_error(line, "reference names a slice the opcode pattern does not define");
  if (vocabularies[*field].members.size() != std::size_t{matched.slices[*found].mask} + 1)
    throw table_error(line, "vocabulary has the wrong number of members for its opcode bits");
  return {.vocabulary_index = static_cast<std::uint8_t>(*field), .slice_index = static_cast<std::uint8_t>(*found)};
}

[[nodiscard]] constexpr Reference reference_from_braces(const std::span<const Vocabulary> vocabularies,
    const std::string_view text, const Pattern &matched, const std::size_t line, const TableDecl &table) {
  if (!text.starts_with('{') || !text.ends_with('}'))
    throw table_error(line, "a reference names a vocabulary and one slice letter, as in {reg:z}");
  return parse_reference(vocabularies, text.substr(1, text.size() - 2), matched, line, table);
}

[[nodiscard]] constexpr Operand parse_operand(const std::span<const Vocabulary> vocabularies,
    const std::string_view word, const Pattern &matched, const std::size_t line, const std::uint8_t immediate_bytes,
    const TableDecl &table) {
  const auto [parameter, text] = split_keyword(word, line);
  auto operand = text.starts_with('{')
                     ? Operand{.kind = Operand::Kind::Vocabulary,
                           .reference = reference_from_braces(vocabularies, text, matched, line, table)}
                     : parse_simple_operand(text, line, immediate_bytes);
  operand.parameter = parameter;
  return operand;
}

constexpr void lower_mnemonic(const std::span<const Vocabulary> vocabularies, Row &row, const TableDecl &table) {
  const auto add = [&row](const Piece piece) {
    if (!row.pieces.try_push_back(piece))
      throw table_error(row.line, "mnemonic is too complicated");
  };
  const auto add_text = [&](const Parser text) {
    for (const auto &piece: pieces_of(text, row.line))
      add(piece);
  };

  Parser parser(row.mnemonic);
  while (!parser.eof()) {
    if (!parser.rest().contains('{')) {
      add_text(parser);
      return;
    }
    add_text(Parser(parser.take_until('{')));
    if (!parser.rest().contains('}'))
      throw table_error(row.line, "unterminated vocabulary reference in mnemonic");
    add({.kind = Piece::Kind::Vocabulary,
        .reference = parse_reference(vocabularies, parser.take_until('}'), row.matched, row.line, table)});
  }
}

// The encoding says what is fetched. The mnemonic must render exactly that, and
// the action must use it: otherwise one of the three columns is lying.
constexpr void check_immediates(const Row &row) {
  // A row fetches one immediate, of `immediate_bytes` bytes, so the mnemonic
  // must render exactly one, of exactly that width. Summing widths would let
  // `$nn $nn` pass against `n n` and then disassemble as two bytes where the
  // machine read one sixteen-bit value.
  std::size_t rendered = 0;
  std::size_t width = 0;
  for (const auto &piece: row.pieces)
    switch (piece.kind) {
      case Piece::Kind::Imm8:
      case Piece::Kind::Relative:
        ++rendered;
        width = 1;
        break;
      case Piece::Kind::Imm16:
        ++rendered;
        width = 2;
        break;
      default: break;
    }
  if (rendered > 1)
    throw table_error(row.line, "a row renders at most one immediate; the encoding only fetches one");
  if (width != row.immediate_bytes)
    throw table_error(row.line, "the mnemonic renders a different number of immediate bytes than the encoding fetches");

  const auto immediate = [](const Operand &operand) { return operand.kind == Operand::Kind::Immediate; };
  const auto uses_immediate = std::ranges::any_of(row.steps, [&](const Step &step) {
    return std::ranges::any_of(step.operands, immediate) || std::ranges::any_of(step.destinations, immediate);
  });
  if (uses_immediate != (row.immediate_bytes != 0))
    throw table_error(row.line, "the action and the encoding disagree about whether there is an immediate");
}

// The first column: the opcode pattern, then whichever bytes the instruction
// carries after it.
constexpr void parse_encoding(Parser encoding, Row &row) {
  row.matched = parse_pattern(encoding.next_word(), row.line);
  while (!encoding.eof()) {
    const auto token = encoding.next_word();
    if (token.empty())
      continue;
    if (token == "d") {
      if (row.reads_displacement)
        throw table_error(row.line, "a row reads at most one displacement");
      row.reads_displacement = true;
      continue;
    }
    if (token != "n")
      throw table_error(row.line, "'" + std::string(token) + "' is not an encoding byte; expected 'n' or 'd'");
    ++row.immediate_bytes;
  }
  if (row.immediate_bytes > 2)
    throw table_error(row.line, "an instruction may carry at most two immediate bytes");
}

// One step of the third column: an operation, the destinations written before
// `<-`, and the operands after it. `if` guards the rest of the row; `goto`
// hands decoding to another table and does nothing else.
[[nodiscard]] constexpr Step parse_step(Parser action, const std::span<const Vocabulary> vocabularies,
    const std::span<const TableDecl> tables, const Row &row, const TableDecl &table) {
  Step step{.operation = action.next_word()};
  if (step.operation == "if") {
    step.kind = Step::Kind::If;
    step.operation = action.next_word();
    if (step.operation.empty())
      throw table_error(row.line, "'if' needs something to test");
  }
  if (step.operation == "goto") {
    if (step.kind == Step::Kind::If)
      throw table_error(row.line, "a goto cannot be conditional; guard it with an earlier `if` step");
    step.kind = Step::Kind::Goto;
    auto destination = action.next_word();
    std::string_view supplied;
    if (const auto open = destination.find('('); open != std::string_view::npos) {
      if (!destination.ends_with(')'))
        throw table_error(row.line, "unterminated '(' in goto");
      supplied = destination.substr(open + 1, destination.size() - open - 2);
      destination = destination.substr(0, open);
    }
    step.target = find_table(tables, destination, row.line);
    if (!action.next_word().empty())
      throw table_error(row.line, "goto takes a single table name");
    const auto &target = tables[step.target];
    if (target.takes_view() && supplied.empty())
      throw table_error(row.line, "this table takes a view, so the goto must say which");
    if (!target.takes_view() && !supplied.empty())
      throw table_error(row.line, "this table takes no view, so the goto may not supply one");
    if (!supplied.empty()) {
      if (table.takes_view() && supplied == table.view_name) {
        if (table.view_vocabulary != target.view_vocabulary)
          throw table_error(row.line, "the view being handed on is drawn from a different vocabulary");
        step.forwards_view = true;
      }
      else {
        const auto &members = vocabularies[target.view_vocabulary].members;
        const auto found = std::ranges::find(members, supplied, &Member::display);
        if (found == members.end())
          throw table_error(row.line, "goto names a view that is not a member of that table's view vocabulary");
        step.target_view = static_cast<std::uint8_t>(found - members.begin());
      }
    }
    return step;
  }
  if (step.operation.starts_with('{'))
    step.operation_reference = reference_from_braces(vocabularies, step.operation, row.matched, row.line, table);
  // `operation dest <- args...`; the destination is optional
  auto writing_destination = action.rest().contains("<-");
  while (!action.eof()) {
    const auto word = trim_comma(action.next_word());
    if (word.empty())
      continue;
    if (word == "<-") {
      writing_destination = false;
      continue;
    }
    const auto operand = parse_operand(vocabularies, word, row.matched, row.line, row.immediate_bytes, table);
    if (writing_destination) {
      if (!operand.parameter.empty())
        throw table_error(row.line, "'" + std::string(operand.parameter.view()) +
                                        "=' names a parameter, and a destination is not one: it is where the result "
                                        "goes, not something handed to the operation");
      if (!step.destinations.try_push_back(operand))
        throw table_error(row.line, "too many destinations");
    }
    else {
      if (operand.kind == Operand::Kind::Discard)
        throw table_error(row.line, "'-' discards a result, so it can only be a destination");
      if (!step.operands.try_push_back(operand))
        throw table_error(row.line, "too many operands");
    }
  }
  return step;
}

[[nodiscard]] constexpr std::vector<Row> parse_rows(const std::string_view description,
    const std::span<const Vocabulary> vocabularies, const std::span<const TableDecl> tables) {
  std::vector<Row> result;
  std::optional<std::uint8_t> current;
  for (const auto [at, text]: lines_of(description)) {
    if (is_table(text)) {
      Parser declaration(text);
      declaration.skip_word();
      current = find_table(tables, table_name_of(declaration.next_word()), at);
      continue;
    }
    if (!is_row(text))
      continue;
    if (!current)
      throw table_error(at, "this row is not in any table; declare one with `table <name>` first");
    // Three columns, separated by `|`: what is encoded, how it reads, what it does.
    Parser parser(text);
    Row row{.table = *current, .line = at};
    parse_encoding(Parser(parser.next_field('|')), row);
    row.mnemonic = parser.next_field('|');
    // Steps run in order, separated by `;`.
    Parser sequence(Parser::trim(parser.rest()));
    while (!sequence.eof()) {
      Parser action(sequence.next_field(';'));
      if (action.eof())
        continue;
      if (!row.steps.try_push_back(parse_step(action, vocabularies, tables, row, tables[*current])))
        throw table_error(at, "row has too many steps");
    }
    if (row.steps.empty())
      throw table_error(at, "row has no action");
    // The disassembler renders nothing for a goto row and stops, so a goto has
    // to be the whole row or the two would disagree about what an opcode means.
    if (std::ranges::any_of(row.steps, [](const Step &step) { return step.kind == Step::Kind::Goto; }) &&
        row.steps.size() != 1) // NOLINT
      throw table_error(at, "a goto must be the row's only step");
    lower_mnemonic(vocabularies, row, tables[*current]);
    check_immediates(row);
    result.push_back(row);
  }
  return result;
}

} // namespace specbolt::refract
