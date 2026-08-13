#pragma once

#ifndef SPECBOLT_MODULES
#include <string_view>
#endif

namespace specbolt::refract {

// A cursor over the description, consuming it from the front. Ordinary text
// handling: everything it hands back is a `std::string_view` into the original,
// and the only state beyond the position is the line number, which is carried
// so that a diagnostic can name where it came from.
SPECBOLT_EXPORT class Parser {
public:
  constexpr explicit Parser(const std::string_view buf, const std::size_t line = 1) : line_(line), buf_(buf) {}

  // Text with leading and trailing blanks removed. A trailing \r matters
  // because the description may have been written on a machine that thinks so.
  [[nodiscard]] static constexpr std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
      text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
      text.remove_suffix(1);
    return text;
  }

  // Everything up to the next `delim`, which is consumed with it; the whole of
  // what is left if there is none.
  [[nodiscard]] constexpr std::string_view take_until(const char delim) {
    const auto pos = buf_.find(delim);
    if (pos == std::string_view::npos) {
      const auto all = buf_;
      consume(buf_.size());
      return all;
    }
    const auto result = buf_.substr(0, pos);
    consume(pos + 1);
    return result;
  }

  // The next `delim`-separated field, blanks removed. A row is three of these.
  [[nodiscard]] constexpr std::string_view next_field(const char delim) { return trim(take_until(delim)); }

  // The next whitespace-separated word, or empty at the end.
  [[nodiscard]] constexpr std::string_view next_word() {
    skip_any(" \t");
    return next_field(' ');
  }

  // For a word that has already been recognised, such as the keyword a
  // declaration opens with.
  constexpr void skip_word() { static_cast<void>(next_word()); }

  // The next line and the number it came from, so a diagnostic can name it.
  struct Line {
    std::size_t number{};
    std::string_view text{};
  };
  [[nodiscard]] constexpr Line next_line() {
    const auto number = line_;
    return {number, next_field('\n')};
  }

  constexpr void skip_any(const std::string_view skip) {
    const auto pos = buf_.find_first_not_of(skip);
    consume(pos == std::string_view::npos ? buf_.size() : pos);
  }

  [[nodiscard]] constexpr bool eof() const { return buf_.empty(); }
  [[nodiscard]] constexpr std::size_t line() const { return line_; }
  // Everything not yet consumed.
  [[nodiscard]] constexpr std::string_view rest() const { return buf_; }

private:
  constexpr void consume(const std::size_t count) {
    for (const auto character: buf_.substr(0, count))
      if (character == '\n')
        ++line_;
    buf_ = buf_.substr(count);
  }

  std::size_t line_{1};
  std::string_view buf_;
};

} // namespace specbolt::refract
