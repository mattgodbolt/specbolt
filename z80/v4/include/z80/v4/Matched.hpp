#pragma once

#ifndef SPECBOLT_MODULES
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#endif

namespace specbolt::v4 {

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

SPECBOLT_EXPORT struct Matched {
  static constexpr std::size_t max_slices = 4;
  static constexpr std::size_t num_bits = 8;

  std::uint8_t opcode_bits{};
  std::array<BitSlice, max_slices> slices{};
  std::size_t num_slices{};

  [[nodiscard]] constexpr std::uint8_t variable_mask() const {
    std::uint8_t result = 0;
    for (std::size_t index = 0; index < num_slices; ++index)
      result = static_cast<std::uint8_t>(result | slices[index].place(slices[index].mask));
    return result;
  }
  [[nodiscard]] constexpr std::uint8_t fixed_mask() const { return static_cast<std::uint8_t>(~variable_mask()); }
  [[nodiscard]] constexpr bool matches(const std::uint8_t opcode) const {
    return (opcode & fixed_mask()) == opcode_bits;
  }
};

SPECBOLT_EXPORT [[nodiscard]] constexpr Matched parse_opcode_bits(const std::string_view bits) {
  if (bits.size() != Matched::num_bits)
    throw std::runtime_error("opcode pattern must be 8 characters");
  Matched result;
  for (std::size_t index = 0; index < bits.size(); ++index) {
    const auto bit = static_cast<std::uint8_t>(Matched::num_bits - 1 - index);
    const auto character = bits[index];
    if (character == '0' || character == '1') {
      if (character == '1')
        result.opcode_bits = static_cast<std::uint8_t>(result.opcode_bits | (1u << bit));
      continue;
    }
    auto extended = false;
    for (std::size_t slice = 0; slice < result.num_slices; ++slice) {
      if (result.slices[slice].name != character)
        continue;
      if (result.slices[slice].shift != bit + 1)
        throw std::runtime_error("opcode pattern has non-contiguous bits for a field");
      result.slices[slice].shift = bit;
      result.slices[slice].mask = static_cast<std::uint8_t>((result.slices[slice].mask << 1) | 1);
      extended = true;
      break;
    }
    if (extended)
      continue;
    if (result.num_slices == Matched::max_slices)
      throw std::runtime_error("opcode pattern has too many fields");
    result.slices[result.num_slices++] = BitSlice{character, bit, 1};
  }
  return result;
}

} // namespace specbolt::v4
