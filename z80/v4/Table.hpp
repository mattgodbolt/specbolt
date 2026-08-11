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

struct Field {
  static constexpr std::size_t max_values = 8;
  char name{};
  std::array<std::string_view, max_values> values{};
  std::size_t num_values{};
};

struct Piece {
  enum class Kind : std::uint8_t { Literal, Field, Imm8, Imm16 };
  Kind kind{};
  std::string_view text{};
  std::uint8_t field_index{};
  std::uint8_t slice_index{};
};

struct Row {
  static constexpr std::size_t max_pieces = 8;
  Matched matched{};
  std::string_view mnemonic{};
  std::array<Piece, max_pieces> pieces{};
  std::size_t num_pieces{};
  std::size_t length{1};
  std::string_view verb{};
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
      field.values[field.num_values++] = value;
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
    const auto name = parser.split_to('}').data();
    if (name.size() != 1)
      throw table_error(row.line, "field reference must name a single character");
    const auto field = find_field(name.front());
    if (!field)
      throw table_error(row.line, "mnemonic names a field that does not exist");
    const auto slice = find_slice(row.matched, name.front());
    if (!slice)
      throw table_error(row.line, "mnemonic names a field the opcode pattern does not define");
    if (fields[*field].num_values != std::size_t{row.matched.slices[*slice].mask} + 1)
      throw table_error(row.line, "field has the wrong number of values for its opcode bits");
    push({Piece::Kind::Field, {}, static_cast<std::uint8_t>(*field), static_cast<std::uint8_t>(*slice)});
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
    lower_mnemonic(row);
  }
  return result;
}

inline constexpr auto rows = parse_rows<count_matching(&is_row)>();

[[nodiscard]] constexpr std::optional<std::size_t> find_row(const std::uint8_t opcode) {
  for (std::size_t index = 0; index < rows.size(); ++index)
    if (rows[index].matched.matches(opcode))
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
