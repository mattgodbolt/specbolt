#ifndef SPECBOLT_MODULES
#include "z80/v4/Z80.hpp"

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

struct BitSlice {
  std::uint8_t shift;
  std::uint8_t mask;
};
struct Matched {
  uint8_t opcode_bits;
  std::vector<BitSlice> bit_slices;
  std::string_view instruction;
};

constexpr Matched parse_line(std::string_view line) {
  const auto split = split_on_char(line, '|');
  if (split.size() != 3)
    throw std::runtime_error("Invalid line format '" + std::string(line) + "'");
  Matched result;
  result.instruction = split_line(line);
  return result;
}

constexpr std::vector<Matched> parse_lines(std::string_view lines) {
  std::vector<Matched> result;
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
