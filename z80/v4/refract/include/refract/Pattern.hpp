#pragma once

#ifndef SPECBOLT_MODULES
#include "refract/TableError.hpp"
#include "refract/Vector.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#endif

namespace specbolt::refract {

SPECBOLT_EXPORT struct BitSlice {
  char name{};
  std::uint8_t shift{};
  std::uint8_t mask{};

  [[nodiscard]] constexpr std::uint8_t extract(const std::uint8_t opcode) const {
    return static_cast<std::uint8_t>((opcode >> shift) & mask);
  }
  [[nodiscard]] constexpr std::uint8_t place(const std::uint8_t value) const {
    return static_cast<std::uint8_t>((value & mask) << shift);
  }
  constexpr bool operator==(const BitSlice &) const = default;
};

SPECBOLT_EXPORT struct Pattern {
  static constexpr std::size_t max_slices = 4;
  static constexpr std::size_t num_bits = 8;

  std::uint8_t opcode_bits{};
  Vector<BitSlice, max_slices> slices{};

  [[nodiscard]] constexpr std::uint8_t variable_mask() const {
    std::uint8_t result = 0;
    for (const auto &slice: slices)
      result = static_cast<std::uint8_t>(result | slice.place(slice.mask));
    return result;
  }
  [[nodiscard]] constexpr std::uint8_t fixed_mask() const { return static_cast<std::uint8_t>(~variable_mask()); }
  [[nodiscard]] constexpr bool matches(const std::uint8_t opcode) const {
    return (opcode & fixed_mask()) == opcode_bits;
  }
};

SPECBOLT_EXPORT [[nodiscard]] constexpr Pattern parse_pattern(const std::string_view bits, const std::size_t line) {
  if (bits.size() != Pattern::num_bits)
    throw table_error(line, "opcode pattern must be 8 characters");
  Pattern result;
  for (std::size_t index = 0; index < bits.size(); ++index) {
    const auto bit = static_cast<std::uint8_t>(Pattern::num_bits - 1 - index);
    const auto character = bits[index];
    if (character == '0' || character == '1') {
      if (character == '1')
        result.opcode_bits = static_cast<std::uint8_t>(result.opcode_bits | (1u << bit));
      continue;
    }
    auto extended = false;
    for (auto &slice: result.slices) {
      if (slice.name != character)
        continue;
      if (slice.shift != bit + 1)
        throw table_error(line, "opcode pattern has non-contiguous bits for a slice");
      slice.shift = bit;
      slice.mask = static_cast<std::uint8_t>((slice.mask << 1) | 1);
      extended = true;
      break;
    }
    if (!extended)
      result.slices.push_back({character, bit, 1}, line, "opcode pattern has too many slices");
  }
  return result;
}

} // namespace specbolt::refract
