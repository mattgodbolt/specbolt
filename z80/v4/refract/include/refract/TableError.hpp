#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <stdexcept>
#include <string>
#include <string_view>

namespace specbolt::refract {

// Renders a number for a diagnostic. `std::to_chars` is usable during constant evaluation; `std::to_string` and
// `std::format` are not.
[[nodiscard]] constexpr std::string decimal(const std::size_t value) {
  std::array<char, 20> digits{}; // enough for any 64-bit value
  const auto [end, _] = std::to_chars(digits.data(), digits.data() + digits.size(), value);
  return {digits.data(), end};
}

// A mistake in a description, reported against its line. The parser does not know which file it is reading, so the line
// stands alone here and `naming` puts the file in front of it.
[[nodiscard]] constexpr std::runtime_error table_error(const std::size_t line, const std::string_view what) {
  return std::runtime_error(decimal(line) + ": " + std::string(what));
}

// The same, from a place that knows the file.
[[nodiscard]] constexpr std::runtime_error table_error(
    const std::string_view file, const std::size_t line, const std::string_view what) {
  return std::runtime_error(std::string(file) + ":" + decimal(line) + ": " + std::string(what));
}

// Runs `make`, and if it throws, rethrows with the file in front of the message. Constant evaluation can catch and
// throw since C++26.
template<std::invocable Make>
[[nodiscard]] constexpr auto naming(const std::string_view file, Make make) {
  try {
    return make();
  }
  catch (const std::exception &error) {
    throw std::runtime_error(std::string(file) + ":" + error.what());
  }
}

// Runs `parse`, and if it throws, rethrows with the line in front of the message. Each line of a description is read
// inside one of these, so nothing beneath it needs to know which line that is: a throw says what is wrong, and this
// says where.
template<std::invocable Parse>
[[nodiscard]] constexpr auto at_line(const std::size_t line, Parse parse) {
  try {
    return parse();
  }
  catch (const std::exception &error) {
    throw table_error(line, error.what());
  }
}

} // namespace specbolt::refract
