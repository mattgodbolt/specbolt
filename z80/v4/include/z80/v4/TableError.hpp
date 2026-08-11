#pragma once

#ifndef SPECBOLT_MODULES
#include <stdexcept>
#include <string>
#include <string_view>
#endif

#define SPECBOLT_CPU_TABLE "z80.cpu"

namespace specbolt::v4 {

[[nodiscard]] constexpr std::string decimal(std::size_t value) {
  if (value == 0)
    return "0";
  std::string result;
  while (value != 0) {
    result.insert(result.begin(), static_cast<char>('0' + value % 10));
    value /= 10;
  }
  return result;
}

[[nodiscard]] constexpr std::runtime_error table_error(const std::size_t line, const std::string_view what) {
  return std::runtime_error(SPECBOLT_CPU_TABLE ":" + decimal(line) + ": " + std::string(what));
}

} // namespace specbolt::v4
