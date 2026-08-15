#pragma once

#ifndef SPECBOLT_MODULES
#include <ranges>
#include <string_view>
#include <vector>
#endif

namespace specbolt::refract {

// A cursor over one line of the description, consuming it from the front.
// Ordinary text handling: everything it hands back is a `std::string_view` into
// the original, and the position is the whole of its state.
SPECBOLT_EXPORT class Parser {
public:
  constexpr explicit Parser(const std::string_view buf) : buf_(buf) {}

  // What separates one word from the next. A newline and a backslash are in
  // here because a logical line may span several physical ones: the text still
  // holds the `\` and the newline it was joined at, and neither is a word.
  // A trailing \r matters because the description may have been written on a
  // machine that thinks so.
  static constexpr std::string_view blanks = " \t\r\n\\";

  // Text with leading and trailing blanks removed.
  [[nodiscard]] static constexpr std::string_view trim(const std::string_view text) {
    const auto first = text.find_first_not_of(blanks);
    if (first == std::string_view::npos)
      return {};
    return text.substr(first, text.find_last_not_of(blanks) + 1 - first);
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
    skip_any(blanks);
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
//
// A line ending in `\` continues onto the next, and the two arrive here as one.
// Nothing is copied to do it: the description is one buffer, so a joined line is
// still a single `std::string_view` into it, just a longer one that happens to
// contain the `\` and the newline. Those are blanks to `Parser`, so no consumer
// of a line has to know this happened.
//
// A continued line is reported at the number it *started* on, which is where a
// reader would look for it.
SPECBOLT_EXPORT [[nodiscard]] constexpr std::vector<Line> lines_of(const std::string_view description) {
  // Asked of the untrimmed text, because `trim` would take the `\` away.
  const auto continues = [](const std::string_view raw) {
    const auto last = raw.find_last_not_of(" \t\r");
    return last != std::string_view::npos && raw[last] == '\\';
  };

  std::vector<Line> lines;
  const char *begin = nullptr;
  std::size_t started_at = 0;
  for (const auto [index, part]: description | std::views::split('\n') | std::views::enumerate) {
    const auto number = static_cast<std::size_t>(index) + 1;
    const std::string_view raw{part};
    const auto text = Parser::trim(raw);
    if (!begin) {
      if (text.empty()) {
        lines.push_back({number, {}});
        continue;
      }
      begin = text.data();
      started_at = number;
    }
    if (continues(raw))
      continue;
    // A continuation whose last line is blank has no trimmed text to end at, so
    // the end comes from the untrimmed line instead. The join is trimmed again
    // because that blank line, or a `\` with nothing after it, would otherwise
    // leave trailing blanks inside the text.
    const auto *const end = text.empty() ? raw.data() : text.data() + text.size();
    lines.push_back({started_at, Parser::trim({begin, static_cast<std::size_t>(end - begin)})});
    begin = nullptr;
  }
  // A `\` on the last line has nothing to join to. The text is kept rather than
  // dropped, so whatever is wrong with it is diagnosed by whoever reads it.
  if (begin)
    lines.push_back(
        {started_at, Parser::trim({begin, static_cast<std::size_t>(description.data() + description.size() - begin)})});
  return lines;
}

} // namespace specbolt::refract
