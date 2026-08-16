#pragma once

#include <array>
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>

// The description this build compiles, named by whoever is compiling one. The
// library never names a particular machine's file; diagnostics quote whatever
// the consumer set.
#ifndef SPECBOLT_CPU_TABLE
#define SPECBOLT_CPU_TABLE "cpu"
#endif

namespace specbolt::refract {

// `std::to_string` is not usable during constant evaluation and `std::format`
// is not either; `std::to_chars` has been since C++23.
[[nodiscard]] constexpr std::string decimal(const std::size_t value) {
  std::array<char, 20> digits{}; // enough for any 64-bit value
  const auto [end, _] = std::to_chars(digits.data(), digits.data() + digits.size(), value);
  return {digits.data(), end};
}

// The file and line together, for a message that has to name a second line
// besides the one it is reported against.
[[nodiscard]] constexpr std::string at_line(const std::size_t line) { return SPECBOLT_CPU_TABLE ":" + decimal(line); }

[[nodiscard]] constexpr std::runtime_error table_error(const std::size_t line, const std::string_view what) {
  return std::runtime_error(at_line(line) + ": " + std::string(what));
}

} // namespace specbolt::refract
