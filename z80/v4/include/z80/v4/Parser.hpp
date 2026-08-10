#pragma once

#ifndef SPECBOLT_MODULES
#include <string_view>
#endif

namespace specbolt::v4 {

SPECBOLT_EXPORT class Parser {
public:
  constexpr explicit Parser(const std::string_view buf, const std::size_t line = 1) : line_(line), buf_(buf) {}

  constexpr void skip_any(const std::string_view skip) {
    const auto pos = buf_.find_first_not_of(skip);
    consume(pos == std::string_view::npos ? buf_.size() : pos);
  }

  constexpr Parser split_to(const char delim) {
    const auto pos = buf_.find(delim);
    const auto line = line_;
    if (pos == std::string_view::npos) {
      const auto result = buf_;
      consume(buf_.size());
      return Parser(result, line);
    }
    const auto result = buf_.substr(0, pos);
    consume(pos + 1);
    return Parser(result, line);
  }

  [[nodiscard]] constexpr bool eof() const { return buf_.empty(); }
  [[nodiscard]] constexpr std::size_t line() const { return line_; }
  [[nodiscard]] constexpr std::string_view data() const { return buf_; }

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

} // namespace specbolt::v4
