#pragma once

// Everything that parses a fragment of text without needing the whole
// description: which kind of line this is, what an operand says, what a
// vocabulary member says. Each takes a string and a line number and returns a
// value, so each is testable a line at a time.

#include "refract/Model.hpp"
#include "refract/Parser.hpp"
#include "refract/TableError.hpp"

#include <string>
#include <string_view>

namespace specbolt::refract {

[[nodiscard]] constexpr std::string_view trim_comma(std::string_view text) {
  if (text.ends_with(','))
    text.remove_suffix(1);
  return text;
}

// A keyword on its own is still that keyword, so `table` with no name reaches
// the diagnostic that says so rather than being silently ignored.
[[nodiscard]] constexpr bool is_directive(const std::string_view line, const std::string_view keyword) {
  return line.starts_with(keyword) && (line.size() == keyword.size() || line[keyword.size()] == ' ');
}
[[nodiscard]] constexpr bool is_vocabulary(const std::string_view line) { return is_directive(line, "vocab"); }
[[nodiscard]] constexpr bool is_table(const std::string_view line) { return is_directive(line, "table"); }
[[nodiscard]] constexpr bool is_row(const std::string_view line) {
  return !line.empty() && line.front() != '#' && !is_vocabulary(line) && !is_table(line) && line.contains('|');
}

[[nodiscard]] constexpr std::uint8_t parse_delay(const std::string_view value, const std::size_t line) {
  if (value.size() != 1 || value.front() < '0' || value.front() > '9')
    throw table_error(line, "delay must be a single digit");
  return static_cast<std::uint8_t>(value.front() - '0');
}

[[nodiscard]] constexpr Operand parse_simple_operand(
    std::string_view word, const std::size_t line, const std::uint8_t immediate_bytes) {
  if (word.empty())
    throw table_error(line, "empty operand in action");
  // An addressing mode written out in a row says what it costs the same way a
  // vocabulary member does.
  if (const auto slash = word.find('/'); slash != std::string_view::npos) {
    Parser attribute(word.substr(slash + 1));
    if (attribute.split_to('=').data() != "delay")
      throw table_error(line, "'" + std::string(word.substr(slash + 1)) + "' is not an operand attribute");
    auto attributed = parse_simple_operand(word.substr(0, slash), line, immediate_bytes);
    attributed.write_back_delay = parse_delay(attribute.data(), line);
    return attributed;
  }
  if (word == "-")
    return {.kind = Operand::Kind::Discard};
  if (word.starts_with('(')) {
    if (!word.ends_with(')'))
      throw table_error(line, "unterminated '(' in operand '" + std::string(word) + "'");
    word = word.substr(1, word.size() - 2);
    const auto displaced = word.ends_with("+d");
    if (displaced)
      word.remove_suffix(2);
    auto addressed = parse_simple_operand(word, line, immediate_bytes);
    if (addressed.indirect)
      throw table_error(line, "an address cannot itself be indirect");
    addressed.indirect = true;
    addressed.displaced = displaced;
    return addressed;
  }
  if (word.ends_with("+d"))
    throw table_error(line, "a displacement only makes sense inside '(...)'");
  if (word == "n")
    return {.kind = Operand::Kind::Immediate, .width = immediate_bytes};
  if (word == "nn")
    throw table_error(line, "write 'n'; the encoding column says how many bytes it occupies");
  if (word.front() >= '0' && word.front() <= '9') {
    const auto hex = word.starts_with("0x");
    const auto digits = hex ? word.substr(2) : word;
    const auto base = hex ? 16u : 10u;
    if (digits.empty())
      throw table_error(line, "malformed constant '" + std::string(word) + "'");
    unsigned value = 0;
    for (const auto character: digits) {
      const auto digit = character >= '0' && character <= '9'   ? static_cast<unsigned>(character - '0')
                         : character >= 'a' && character <= 'f' ? static_cast<unsigned>(character - 'a' + 10)
                                                                : base;
      if (digit >= base)
        throw table_error(line, "malformed constant '" + std::string(word) + "'");
      value = value * base + digit;
      if (value > 0xffff)
        throw table_error(line, "constant '" + std::string(word) + "' does not fit in 16 bits");
    }
    return {.kind = Operand::Kind::Constant, .constant = static_cast<std::uint16_t>(value)};
  }
  if (word.size() > Name::capacity)
    throw table_error(line, "operand name '" + std::string(word) + "' is too long");
  return {.kind = Operand::Kind::Named, .name = Name{word}};
}

// Splits display text around the values it renders rather than spells: `$nn`
// and `$nnnn` come from the encoding, `+d` is the displacement an indexed mode
// carries. Both a row's mnemonic and a vocabulary member's text are lowered
// with this, so neither is parsed at runtime.
constexpr void lower_text(Parser text, const auto &push, const std::size_t line) {
  const auto push_immediates = [&](Parser chunk) {
    while (!chunk.eof()) {
      if (!chunk.data().contains('$')) {
        if (!chunk.data().empty())
          push(Piece{.kind = Piece::Kind::Literal, .text = chunk.data()});
        return;
      }
      if (const auto literal = chunk.split_to('$').data(); !literal.empty())
        push(Piece{.kind = Piece::Kind::Literal, .text = literal});
      if (chunk.data().starts_with('e')) {
        chunk.skip_any("e");
        push(Piece{.kind = Piece::Kind::Relative});
        continue;
      }
      const auto remaining = chunk.data().size();
      chunk.skip_any("n");
      switch (remaining - chunk.data().size()) {
        case 2: push(Piece{.kind = Piece::Kind::Imm8}); break;
        case 4: push(Piece{.kind = Piece::Kind::Imm16}); break;
        default: throw table_error(line, "expected $nn, $nnnn or $e in mnemonic");
      }
    }
  };

  while (!text.eof()) {
    const auto at = text.data().find("+d");
    if (at == std::string_view::npos) {
      push_immediates(text);
      return;
    }
    push_immediates(Parser(text.data().substr(0, at)));
    push(Piece{.kind = Piece::Kind::Displacement});
    text = Parser(text.data().substr(at + 2));
  }
}

// `bc` is display only; `adc:add8+carry` binds an operation and appends an
// operand; `(hl)/delay=1` states the access sequence of an addressing mode.
[[nodiscard]] constexpr Member parse_member(const std::string_view text, const std::size_t line) {
  Parser whole(text);
  Parser parser(whole.split_to('/').data());
  Member member{.display = parser.split_to(':').data(), .operation = parser.data()};
  std::uint8_t delay_attribute = 0;
  if (const auto attributes = whole.data(); !attributes.empty()) {
    Parser attribute(attributes);
    const auto key = attribute.split_to('=').data();
    const auto value = attribute.data();
    if (key != "delay")
      throw table_error(line, "'" + std::string(key) + "' is not a member attribute; expected 'delay'");
    delay_attribute = parse_delay(value, line);
  }
  if (member.display.empty())
    throw table_error(line, "a vocabulary member has no name");
  if (member.display.contains('$'))
    throw table_error(line, "a vocabulary member cannot render an immediate; only the encoding fetches those");
  if (member.display == "-") {
    member.hole = true;
    return member;
  }
  lower_text(
      Parser(member.display),
      [&](const Piece piece) { member.pieces.push_back(piece, line, "member text is too complicated"); }, line);
  member.operand = parse_simple_operand(member.display, line, 0);
  member.operand.write_back_delay = delay_attribute;
  if (member.operand.kind == Operand::Kind::Immediate || member.operand.kind == Operand::Kind::Discard)
    throw table_error(line, "a vocabulary member must name something the CPU can resolve");
  Parser operation(member.operation);
  member.operation = operation.split_to('+').data();
  if (const auto appended = operation.data(); !appended.empty()) {
    member.appended = parse_simple_operand(appended, line, 0);
    if (member.appended->kind == Operand::Kind::Immediate)
      throw table_error(line, "a vocabulary member cannot append an immediate; only the encoding fetches those");
  }
  return member;
}

} // namespace specbolt::refract
