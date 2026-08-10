#ifndef SPECBOLT_MODULES
#include "z80/v4/Z80.hpp"
#include "z80/v4/Matched.hpp"
#include "z80/v4/Parser.hpp"

#include <meta>
#include <span>
#include <string_view>
#include <vector>

#endif

namespace specbolt::v4 {
// clang-format off
constexpr char z80_raw[] = {
#embed "z80.cpu"

  , 0
};
// clang-format on

constexpr std::string_view z80_description{z80_raw};

constexpr std::string_view split_on_char(std::string_view &buf, char character) {
  const auto end = buf.find(character);
  if (end == std::string_view::npos) {
    const auto result = buf;
    buf = {};
    return result;
  }
  const auto result = buf.substr(0, end);
  buf = buf.substr(end + 1);
  return result;
}
constexpr std::string_view split_line(std::string_view &buf) { return split_on_char(buf, '\n'); }

struct CompileTimeString {
  const char *text;
  std::size_t length;
  explicit consteval CompileTimeString(std::string_view line) :
      text(std::define_static_string(line)), length(line.size()) {}
};

struct ParsedLine {
  Matched matched;
  std::string_view instruction;
  std::string_view actions;
};

constexpr ParsedLine parse_line(const std::string_view line) {
  Parser parser(line);
  auto bits = parser.split_to('|');
  bits.skip_any(" ");
  const auto instruction = parser.split_to('|');
  return {parse_opcode_bits(bits.split_to(' ').data()), instruction.data(), parser.data()};
}

constexpr std::vector<ParsedLine> parse_lines(std::string_view lines) {
  std::vector<ParsedLine> result;
  while (!lines.empty()) {
    auto line = split_line(lines);
    // line = skipws(line);
    if (line.empty() || line[0] == '#')
      continue;
    result.push_back(parse_line(line));
  }
  return result;
}

consteval std::vector<CompileTimeString> split_lines(std::string_view buf) {
  std::vector<CompileTimeString> result;
  while (!buf.empty()) {
    result.push_back(CompileTimeString{split_line(buf)});
  }
  return result;
}


constexpr std::span<const CompileTimeString> lines = std::define_static_array(split_lines(z80_description));

size_t Z80::test() { return lines.size(); }

} // namespace specbolt::v4
