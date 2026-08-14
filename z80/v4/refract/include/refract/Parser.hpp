#pragma once

#ifndef SPECBOLT_MODULES
#include <ranges>
#include <string_view>
#endif

namespace specbolt::refract {

// A cursor over one line of the description, consuming it from the front.
// Ordinary text handling: everything it hands back is a `std::string_view` into
// the original, and the position is the whole of its state.
SPECBOLT_EXPORT class Parser {
public:
  constexpr explicit Parser(const std::string_view buf) : buf_(buf) {}

  // Text with leading and trailing blanks removed. A trailing \r matters
  // because the description may have been written on a machine that thinks so.
  [[nodiscard]] static constexpr std::string_view trim(const std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
      return {};
    return text.substr(first, text.find_last_not_of(" \t\r") + 1 - first);
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

  constexpr void skip_any(const std::string_view skip) {
    const auto pos = buf_.find_first_not_of(skip);
    consume(pos == std::string_view::npos ? buf_.size() : pos);
  }

  [[nodiscard]] constexpr bool eof() const { return buf_.empty(); }
  // Everything not yet consumed.
  [[nodiscard]] constexpr std::string_view rest() const { return buf_; }

private:
  constexpr void consume(const std::size_t count) { buf_ = buf_.substr(count); }

  std::string_view buf_;
};

// A line, and the number a diagnostic names it by.
SPECBOLT_EXPORT struct Line {
  std::size_t number{};
  std::string_view text{};
};

// A description is its lines, numbered from one. Every pass over the text wants
// exactly this, and this is the only place that knows lines are numbered at all.
SPECBOLT_EXPORT [[nodiscard]] constexpr auto lines_of(const std::string_view description) {
  return description | std::views::split('\n') | std::views::enumerate | std::views::transform([](const auto numbered) {
    const auto &[index, text] = numbered;
    return Line{static_cast<std::size_t>(index) + 1, Parser::trim(std::string_view{text})};
  });
}

} // namespace specbolt::refract
