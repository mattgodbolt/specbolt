#pragma once

// The three kinds of declaration a description contains, each reading the whole
// text and returning a fixed-size array of what it found.

#include "z80/v4/Lower.hpp"
#include "z80/v4/Matched.hpp"
#include "z80/v4/Model.hpp"
#include "z80/v4/Parser.hpp"
#include "z80/v4/TableError.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace specbolt::v4 {

template<std::size_t N>
[[nodiscard]] constexpr std::array<Field, N> parse_fields(const std::string_view description) {
  std::array<Field, N> result{};
  Parser lines(description);
  std::size_t index = 0;
  while (!lines.eof()) {
    const auto [at, text] = lines.next_line();
    if (!is_field(text))
      continue;
    Parser parser(text);
    static_cast<void>(parser.next_word());
    if (index == N)
      throw table_error(at, "more field declarations than the count that sized this array");
    auto &field = result[index++];
    const auto name = parser.next_word();
    if (name.size() != 1)
      throw table_error(at, "field name must be a single character");
    field.name = name.front();
    if (parser.next_word() != "=")
      throw table_error(at, "expected '=' in field declaration");
    while (!parser.eof()) {
      const auto value = parser.next_word();
      if (value.empty())
        continue;
      field.values.push_back(parse_member(value, at), at, "too many values in field");
    }
    if (field.values.empty())
      throw table_error(at, "field declares no values");
    for (std::size_t other = 0; other + 1 < index; ++other)
      if (result[other].name == field.name)
        throw table_error(at, "duplicate field name");
  }
  return result;
}


// After blanks and comments, every line is a declaration or a row. A line that
// is neither is a mistyped one of them -- a row that lost its separators, or
// `fields` for `field` -- and would otherwise be skipped in silence, surfacing
// much later as an opcode nothing decodes.
constexpr bool check_every_line_means_something(const std::string_view description) {
  Parser lines(description);
  while (!lines.eof()) {
    const auto [at, text] = lines.next_line();
    if (text.empty() || text.front() == '#' || is_field(text) || is_table(text) || is_row(text))
      continue;
    throw table_error(at, "this is not a comment, a declaration, or a row; a row needs its '|' separators");
  }
  return true;
}

[[nodiscard]] constexpr std::optional<std::size_t> find_field(const std::span<const Field> fields, const char name) {
  for (std::size_t index = 0; index < fields.size(); ++index)
    if (fields[index].name == name)
      return index;
  return std::nullopt;
}

// `p.hl->ix, r.h -> ixh`: either spacing, because both read naturally.
constexpr void parse_substitutions(
    const std::string_view text, const std::span<const Field> fields, TableDecl &table, const std::size_t line) {
  Parser list(text);
  while (!list.eof()) {
    const auto rule = Parser::trim(list.split_to(',').data());
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
      throw table_error(line, "a table substitution names the vocabulary it rewrites, as in 'r.h -> ixh'");
    const auto vocabulary = left.substr(0, dot);
    if (vocabulary.size() != 1)
      throw table_error(line, "vocabulary name must be a single character");
    const auto field = find_field(fields, vocabulary.front());
    if (!field)
      throw table_error(line, "substitution names a vocabulary that does not exist");
    const auto from = left.substr(dot + 1);
    if (from.empty())
      throw table_error(line, "a table substitution needs a name on each side of '->'");
    if (std::ranges::none_of(fields[*field].values, [&](const Member &m) { return m.display == from; }))
      throw table_error(line, "vocabulary '" + std::string(vocabulary) + "' has no member '" + std::string(from) + "'");
    // Coverage is worked out before any rule is applied, so a row renamed to
    // nothing would still claim its opcodes and then resolve to a default zero.
    const auto replacement = parse_member(to, line);
    if (replacement.hole)
      throw table_error(line, "a substitution cannot rename something to nothing; a hole belongs in a vocabulary");
    table.rules.push_back(
        {static_cast<std::uint8_t>(*field), from, replacement}, line, "too many substitutions in table");
  }
}

template<std::size_t N>
[[nodiscard]] constexpr std::array<TableDecl, N> parse_tables(
    const std::string_view description, const std::span<const Field> fields) {
  std::array<TableDecl, N> result{};
  Parser lines(description);
  std::size_t index = 0;
  while (!lines.eof()) {
    const auto [at, text] = lines.next_line();
    if (!is_table(text))
      continue;
    Parser parser(text);
    static_cast<void>(parser.next_word());
    const auto name = parser.next_word();
    if (name.empty())
      throw table_error(at, "table declaration has no name");
    for (std::size_t other = 0; other < index; ++other)
      if (result[other].name == name)
        throw table_error(at, "duplicate table name");
    if (index == N)
      throw table_error(at, "more table declarations than the count that sized this array");
    auto &table = result[index++];
    table = {.name = name, .line = at};
    if (const auto equals = parser.next_word(); equals.empty())
      continue;
    else if (equals != "=")
      throw table_error(at, "expected '= <parent> with <substitutions>' after the table name");
    // Only a table already declared, which makes the derivation a forest: a
    // parent's own rows are resolved before anything inherits them.
    const auto parent = parser.next_word();
    table.derived = true;
    table.parent = 0xff;
    for (std::size_t other = 0; other + 1 < index; ++other)
      if (result[other].name == parent)
        table.parent = static_cast<std::uint8_t>(other);
    if (table.parent == 0xff)
      throw table_error(at, "no table named '" + std::string(parent) + "' is declared above this one");
    if (parser.next_word() != "with")
      throw table_error(at, "expected 'with' after the parent table name");
    parse_substitutions(parser.data(), fields, table, at);
    if (table.rules.empty())
      throw table_error(at, "a derived table declares no substitutions, so it is its parent");
  }
  return result;
}

[[nodiscard]] constexpr std::uint8_t find_table(
    const std::span<const TableDecl> tables, const std::string_view name, const std::size_t line) {
  for (std::size_t index = 0; index < tables.size(); ++index)
    if (tables[index].name == name)
      return static_cast<std::uint8_t>(index);
  throw table_error(line, "no table named '" + std::string(name) + "'");
}

[[nodiscard]] constexpr std::optional<std::size_t> find_slice(const Matched &matched, const char name) {
  const auto found = std::ranges::find(matched.slices, name, &BitSlice::name);
  if (found == matched.slices.end())
    return std::nullopt;
  return static_cast<std::size_t>(found - matched.slices.begin());
}

// `{p}` names one letter for both; `{r:z}` binds vocabulary r to slice z.
[[nodiscard]] constexpr Reference parse_reference(
    const std::span<const Field> fields, const std::string_view inner, const Matched &matched, const std::size_t line) {
  Parser parser(inner);
  const auto vocabulary = parser.split_to(':').data();
  const auto slice = parser.eof() ? vocabulary : parser.data();
  if (vocabulary.size() != 1 || slice.size() != 1)
    throw table_error(line, "reference must be {x} or {vocabulary:slice}");
  const auto field = find_field(fields, vocabulary.front());
  if (!field)
    throw table_error(line, "reference names a vocabulary that does not exist");
  const auto found = find_slice(matched, slice.front());
  if (!found)
    throw table_error(line, "reference names a field the opcode pattern does not define");
  if (fields[*field].values.size() != std::size_t{matched.slices[*found].mask} + 1)
    throw table_error(line, "vocabulary has the wrong number of values for its opcode bits");
  return {static_cast<std::uint8_t>(*field), static_cast<std::uint8_t>(*found)};
}

[[nodiscard]] constexpr Reference reference_from_braces(
    const std::span<const Field> fields, const std::string_view text, const Matched &matched, const std::size_t line) {
  if (!text.starts_with('{') || !text.ends_with('}'))
    throw table_error(line, "reference must be {x} or {vocabulary:slice}");
  return parse_reference(fields, text.substr(1, text.size() - 2), matched, line);
}

[[nodiscard]] constexpr Operand parse_operand(const std::span<const Field> fields, const std::string_view word,
    const Matched &matched, const std::size_t line, const std::uint8_t immediate_bytes) {
  if (!word.starts_with('{'))
    return parse_simple_operand(word, line, immediate_bytes);
  return {.kind = Operand::Kind::Field, .reference = reference_from_braces(fields, word, matched, line)};
}

constexpr void lower_mnemonic(const std::span<const Field> fields, Row &row) {
  const auto push = [&row](const Piece piece) { row.pieces.push_back(piece, row.line, "mnemonic is too complicated"); };
  const auto push_text = [&](const Parser text) { lower_text(text, push, row.line); };

  Parser parser(row.mnemonic);
  while (!parser.eof()) {
    if (!parser.data().contains('{')) {
      push_text(parser);
      return;
    }
    push_text(Parser(parser.split_to('{').data()));
    if (!parser.data().contains('}'))
      throw table_error(row.line, "unterminated field reference in mnemonic");
    push({.kind = Piece::Kind::Field,
        .reference = parse_reference(fields, parser.split_to('}').data(), row.matched, row.line)});
  }
}

// The encoding says what is fetched. The mnemonic must render exactly that, and
// the action must use it: otherwise one of the three columns is lying.
constexpr void check_immediates(const Row &row) {
  // A row fetches one immediate, of `immediate_bytes` bytes -- so the mnemonic
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

template<std::size_t N>
[[nodiscard]] constexpr std::array<Row, N> parse_rows(
    const std::string_view description, const std::span<const Field> fields, const std::span<const TableDecl> tables) {
  std::array<Row, N> result{};
  Parser lines(description);
  std::size_t index = 0;
  std::optional<std::uint8_t> current;
  while (!lines.eof()) {
    const auto [at, text] = lines.next_line();
    if (is_table(text)) {
      Parser declaration(text);
      static_cast<void>(declaration.next_word());
      current = find_table(tables, declaration.next_word(), at);
      continue;
    }
    if (!is_row(text))
      continue;
    if (!current)
      throw table_error(at, "this row is not in any table; declare one with `table <name>` first");
    Parser parser(text, at);
    if (index == N)
      throw table_error(at, "more rows than the count that sized this array");
    auto &row = result[index++];
    row.line = at;
    row.table = *current;
    Parser encoding(Parser::trim(parser.split_to('|').data()), at);
    row.matched = parse_opcode_bits(encoding.next_word(), at);
    while (!encoding.eof()) {
      const auto token = encoding.next_word();
      if (token.empty())
        continue;
      if (token == "d") {
        if (row.reads_displacement)
          throw table_error(at, "a row reads at most one displacement");
        row.reads_displacement = true;
        continue;
      }
      if (token != "n")
        throw table_error(at, "'" + std::string(token) + "' is not an encoding byte; expected 'n' or 'd'");
      ++row.immediate_bytes;
    }
    if (row.immediate_bytes > 2)
      throw table_error(at, "an instruction may carry at most two immediate bytes");
    row.mnemonic = Parser::trim(parser.split_to('|').data());
    // steps run in order, separated by `;`
    Parser sequence(Parser::trim(parser.data()));
    while (!sequence.eof()) {
      Parser action(Parser::trim(sequence.split_to(';').data()));
      if (action.eof())
        continue;
      row.steps.push_back({}, at, "row has too many steps");
      auto &step = row.steps[row.steps.size() - 1];
      step.verb = action.next_word();
      if (step.verb == "if") {
        step.kind = Step::Kind::If;
        step.verb = action.next_word();
        if (step.verb.empty())
          throw table_error(at, "'if' needs something to test");
      }
      if (step.verb == "goto") {
        if (step.kind == Step::Kind::If)
          throw table_error(at, "a goto cannot be conditional; guard it with an earlier `if` step");
        step.kind = Step::Kind::Goto;
        step.target = find_table(tables, action.next_word(), at);
        if (!action.next_word().empty())
          throw table_error(at, "goto takes a single table name");
        continue;
      }
      if (step.verb.starts_with('{'))
        step.verb_reference = reference_from_braces(fields, step.verb, row.matched, at);
      // `verb dest <- args...`; the destination is optional
      auto writing_destination = action.data().contains("<-");
      while (!action.eof()) {
        const auto word = trim_comma(action.next_word());
        if (word.empty())
          continue;
        if (word == "<-") {
          writing_destination = false;
          continue;
        }
        const auto operand = parse_operand(fields, word, row.matched, at, row.immediate_bytes);
        if (writing_destination) {
          step.destinations.push_back(operand, at, "too many destinations");
        }
        else {
          if (operand.kind == Operand::Kind::Discard)
            throw table_error(at, "'-' discards a result, so it can only be a destination");
          step.operands.push_back(operand, at, "too many operands");
        }
      }
    }
    if (row.steps.empty())
      throw table_error(at, "row has no action");
    // The disassembler renders nothing for a goto row and stops, so a goto has
    // to be the whole row or the two would disagree about what an opcode means.
    if (std::ranges::any_of(row.steps, [](const Step &step) { return step.kind == Step::Kind::Goto; }) &&
        row.steps.size() != 1) // NOLINT
      throw table_error(at, "a goto must be the row's only step");
    lower_mnemonic(fields, row);
    check_immediates(row);
  }
  return result;
}

} // namespace specbolt::v4
