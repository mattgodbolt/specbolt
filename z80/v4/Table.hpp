#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/v4/Matched.hpp"
#include "z80/v4/Parser.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#endif

namespace specbolt::v4 {

// clang-format off
inline constexpr char cpu_raw[] = {
#embed "z80.cpu"
  , 0
};
// clang-format on

inline constexpr std::string_view cpu_description{cpu_raw};

enum class CarrySource : std::uint8_t { Zero, FromFlags };

struct Member {
  std::string_view display{};
  std::string_view primitive{};
  CarrySource carry{};
  bool hole{};
};

struct Reference {
  std::uint8_t field_index{};
  std::uint8_t slice_index{};
};

struct Field {
  static constexpr std::size_t max_values = 8;
  char name{};
  std::array<Member, max_values> values{};
  std::size_t num_values{};
};

struct Piece {
  enum class Kind : std::uint8_t { Literal, Field, Imm8, Imm16 };
  Kind kind{};
  std::string_view text{};
  std::uint8_t field_index{};
  std::uint8_t slice_index{};
};

struct Operand {
  enum class Kind : std::uint8_t { Accumulator, Immediate, Field };
  Kind kind{};
  std::uint8_t field_index{};
  std::uint8_t slice_index{};
};

struct Row {
  static constexpr std::size_t max_pieces = 12;
  static constexpr std::size_t max_operands = 4;
  Matched matched{};
  std::string_view mnemonic{};
  std::array<Piece, max_pieces> pieces{};
  std::size_t num_pieces{};
  std::size_t length{1};
  std::string_view verb{};
  std::optional<Reference> verb_reference{};
  std::optional<Operand> destination{};
  std::array<Operand, max_operands> operands{};
  std::size_t num_operands{};
  std::size_t line{};
};

[[nodiscard]] consteval std::string decimal(std::size_t value) {
  if (value == 0)
    return "0";
  std::string result;
  while (value != 0) {
    result.insert(result.begin(), static_cast<char>('0' + value % 10));
    value /= 10;
  }
  return result;
}

[[nodiscard]] consteval std::runtime_error table_error(const std::size_t line, const std::string_view what) {
  return std::runtime_error("z80.cpu:" + decimal(line) + ": " + std::string(what));
}

[[nodiscard]] constexpr std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    text.remove_suffix(1);
  return text;
}

[[nodiscard]] constexpr std::string_view next_word(Parser &parser) {
  parser.skip_any(" \t");
  return trim(parser.split_to(' ').data());
}

[[nodiscard]] constexpr bool is_field(const std::string_view line) { return line.starts_with("field "); }
[[nodiscard]] constexpr bool is_row(const std::string_view line) {
  return !line.empty() && line.front() != '#' && !is_field(line) && line.contains('|');
}

[[nodiscard]] consteval std::size_t count_matching(bool (*predicate)(std::string_view)) {
  Parser parser(cpu_description);
  std::size_t count = 0;
  while (!parser.eof())
    if (predicate(trim(parser.split_to('\n').data())))
      ++count;
  return count;
}

// `bc` is display only; `adc:add8+c` binds to a primitive and says the carry comes from the flags.
[[nodiscard]] consteval Member parse_member(const std::string_view text, const std::size_t line) {
  Parser parser(text);
  Member member{parser.split_to(':').data(), parser.data(), CarrySource::Zero, false};
  if (member.display.empty())
    throw table_error(line, "field member has no name");
  if (member.display == "-") {
    member.hole = true;
    return member;
  }
  if (member.primitive.ends_with("+c")) {
    member.primitive.remove_suffix(2);
    member.carry = CarrySource::FromFlags;
  }
  return member;
}

template<std::size_t N>
[[nodiscard]] consteval std::array<Field, N> parse_fields() {
  std::array<Field, N> result{};
  Parser lines(cpu_description);
  std::size_t index = 0;
  while (!lines.eof()) {
    const auto at = lines.line();
    const auto text = trim(lines.split_to('\n').data());
    if (!is_field(text))
      continue;
    Parser parser(text);
    static_cast<void>(next_word(parser));
    auto &field = result[index++];
    const auto name = next_word(parser);
    if (name.size() != 1)
      throw table_error(at, "field name must be a single character");
    field.name = name.front();
    if (next_word(parser) != "=")
      throw table_error(at, "expected '=' in field declaration");
    while (!parser.eof()) {
      const auto value = next_word(parser);
      if (value.empty())
        continue;
      if (field.num_values == Field::max_values)
        throw table_error(at, "too many values in field");
      field.values[field.num_values++] = parse_member(value, at);
    }
    if (field.num_values == 0)
      throw table_error(at, "field declares no values");
    for (std::size_t other = 0; other + 1 < index; ++other)
      if (result[other].name == field.name)
        throw table_error(at, "duplicate field name");
  }
  return result;
}

inline constexpr auto fields = parse_fields<count_matching(&is_field)>();

[[nodiscard]] constexpr std::optional<std::size_t> find_field(const char name) {
  for (std::size_t index = 0; index < fields.size(); ++index)
    if (fields[index].name == name)
      return index;
  return std::nullopt;
}

[[nodiscard]] constexpr std::optional<std::size_t> find_slice(const Matched &matched, const char name) {
  for (std::size_t index = 0; index < matched.num_slices; ++index)
    if (matched.slices[index].name == name)
      return index;
  return std::nullopt;
}

// `{p}` names one letter for both; `{r:z}` binds vocabulary r to slice z.
[[nodiscard]] consteval Reference parse_reference(
    const std::string_view inner, const Matched &matched, const std::size_t line) {
  Parser parser(inner);
  const auto vocabulary = parser.split_to(':').data();
  const auto slice = parser.eof() ? vocabulary : parser.data();
  if (vocabulary.size() != 1 || slice.size() != 1)
    throw table_error(line, "reference must be {x} or {vocabulary:slice}");
  const auto field = find_field(vocabulary.front());
  if (!field)
    throw table_error(line, "reference names a vocabulary that does not exist");
  const auto found = find_slice(matched, slice.front());
  if (!found)
    throw table_error(line, "reference names a field the opcode pattern does not define");
  if (fields[*field].num_values != std::size_t{matched.slices[*found].mask} + 1)
    throw table_error(line, "vocabulary has the wrong number of values for its opcode bits");
  return {static_cast<std::uint8_t>(*field), static_cast<std::uint8_t>(*found)};
}

[[nodiscard]] consteval Reference reference_from_braces(
    const std::string_view text, const Matched &matched, const std::size_t line) {
  if (!text.starts_with('{') || !text.ends_with('}'))
    throw table_error(line, "reference must be {x} or {vocabulary:slice}");
  return parse_reference(text.substr(1, text.size() - 2), matched, line);
}

[[nodiscard]] consteval Operand parse_operand(
    const std::string_view word, const Matched &matched, const std::size_t line) {
  if (word == "a")
    return {Operand::Kind::Accumulator, 0, 0};
  if (word == "n" || word == "nn")
    return {Operand::Kind::Immediate, 0, 0};
  if (!word.starts_with('{'))
    throw table_error(line, "unknown operand '" + std::string(word) + "' in action");
  const auto reference = reference_from_braces(word, matched, line);
  return {Operand::Kind::Field, reference.field_index, reference.slice_index};
}

consteval void lower_mnemonic(Row &row) {
  const auto push = [&row](const Piece piece) {
    if (row.num_pieces == Row::max_pieces)
      throw table_error(row.line, "mnemonic is too complicated");
    row.pieces[row.num_pieces++] = piece;
  };
  const auto push_text = [&](Parser text) {
    while (!text.eof()) {
      if (!text.data().contains('$')) {
        push({Piece::Kind::Literal, text.data(), 0, 0});
        return;
      }
      if (const auto literal = text.split_to('$').data(); !literal.empty())
        push({Piece::Kind::Literal, literal, 0, 0});
      const auto remaining = text.data().size();
      text.skip_any("n");
      switch (remaining - text.data().size()) {
        case 2:
          push({Piece::Kind::Imm8, {}, 0, 0});
          row.length += 1;
          break;
        case 4:
          push({Piece::Kind::Imm16, {}, 0, 0});
          row.length += 2;
          break;
        default: throw table_error(row.line, "expected $nn or $nnnn in mnemonic");
      }
    }
  };

  Parser parser(row.mnemonic);
  while (!parser.eof()) {
    if (!parser.data().contains('{')) {
      push_text(parser);
      return;
    }
    push_text(Parser(parser.split_to('{').data()));
    if (!parser.data().contains('}'))
      throw table_error(row.line, "unterminated field reference in mnemonic");
    const auto reference = parse_reference(parser.split_to('}').data(), row.matched, row.line);
    push({Piece::Kind::Field, {}, reference.field_index, reference.slice_index});
  }
}

template<std::size_t N>
[[nodiscard]] consteval std::array<Row, N> parse_rows() {
  std::array<Row, N> result{};
  Parser lines(cpu_description);
  std::size_t index = 0;
  while (!lines.eof()) {
    const auto at = lines.line();
    const auto text = trim(lines.split_to('\n').data());
    if (!is_row(text))
      continue;
    Parser parser(text, at);
    auto &row = result[index++];
    row.line = at;
    row.matched = parse_opcode_bits(trim(parser.split_to('|').data()));
    row.mnemonic = trim(parser.split_to('|').data());
    Parser action(trim(parser.data()));
    row.verb = next_word(action);
    if (row.verb.empty())
      throw table_error(at, "row has no action");
    if (row.verb.starts_with('{'))
      row.verb_reference = reference_from_braces(row.verb, row.matched, at);
    // `verb dest <- args...`; the destination is optional
    auto writing_destination = action.data().contains("<-");
    while (!action.eof()) {
      const auto word = next_word(action);
      if (word.empty())
        continue;
      if (word == "<-") {
        writing_destination = false;
        continue;
      }
      const auto operand = parse_operand(word, row.matched, at);
      if (writing_destination) {
        if (row.destination)
          throw table_error(at, "an action may only have one destination");
        row.destination = operand;
      }
      else {
        if (row.num_operands == Row::max_operands)
          throw table_error(at, "too many operands");
        row.operands[row.num_operands++] = operand;
      }
    }
    lower_mnemonic(row);
  }
  return result;
}

inline constexpr auto rows = parse_rows<count_matching(&is_row)>();

// A row matches only if the bits fit AND every vocabulary member it names is live:
// a `-` member is a hole, so the row simply does not cover that opcode.
[[nodiscard]] constexpr bool row_matches(const Row &row, const std::uint8_t opcode) {
  if (!row.matched.matches(opcode))
    return false;
  const auto live = [&](const Reference reference) {
    return !fields[reference.field_index].values[row.matched.slices[reference.slice_index].extract(opcode)].hole;
  };
  for (std::size_t at = 0; at < row.num_pieces; ++at)
    if (row.pieces[at].kind == Piece::Kind::Field && !live({row.pieces[at].field_index, row.pieces[at].slice_index}))
      return false;
  for (std::size_t at = 0; at < row.num_operands; ++at)
    if (row.operands[at].kind == Operand::Kind::Field &&
        !live({row.operands[at].field_index, row.operands[at].slice_index}))
      return false;
  if (row.destination && row.destination->kind == Operand::Kind::Field &&
      !live({row.destination->field_index, row.destination->slice_index}))
    return false;
  if (row.verb_reference && !live(*row.verb_reference))
    return false;
  return true;
}

[[nodiscard]] constexpr std::optional<std::size_t> find_row(const std::uint8_t opcode) {
  for (std::size_t index = 0; index < rows.size(); ++index)
    if (row_matches(rows[index], opcode))
      return index;
  return std::nullopt;
}

[[nodiscard]] constexpr std::uint8_t field_value(const Row &row, const char name, const std::uint8_t opcode) {
  const auto slice = find_slice(row.matched, name);
  if (!slice)
    throw std::runtime_error("mnemonic references a field the pattern does not define");
  return row.matched.slices[*slice].extract(opcode);
}

} // namespace specbolt::v4
