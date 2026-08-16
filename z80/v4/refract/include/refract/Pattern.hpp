#pragma once

#include "refract/TableError.hpp"
#include "refract/Vector.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace specbolt::refract {

struct BitSlice {
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

struct Pattern {
  static constexpr std::size_t max_slices = 4;
  static constexpr std::size_t num_bits = 8;

  std::uint8_t opcode_bits{};
  Vector<BitSlice, max_slices> slices{};
};

[[nodiscard]] constexpr Pattern parse_pattern(const std::string_view bits, const std::size_t line) {
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
    const auto found = std::ranges::find(result.slices, character, &BitSlice::name);
    if (found == result.slices.end()) {
      if (!result.slices.try_push_back({character, bit, 1}))
        throw table_error(line, "opcode pattern has too many slices");
      continue;
    }
    if (found->shift != bit + 1)
      throw table_error(line, "opcode pattern has non-contiguous bits for a slice");
    found->shift = bit;
    found->mask = static_cast<std::uint8_t>((found->mask << 1) | 1);
  }
  return result;
}

} // namespace specbolt::refract
