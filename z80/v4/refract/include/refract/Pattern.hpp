#pragma once

// The opcode pattern a row's encoding column opens with: eight characters,
// each a `0` or `1` fixing that bit or a letter naming a slice, a run of one
// letter being one slice.

#include "refract/TableError.hpp"
#include "refract/Vector.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace specbolt::refract {

// A run of bits in the opcode, named by its letter. `extract` reads what an
// opcode carries there and `place` puts a value back, so `place(extract(x))`
// is the slice's bits of `x` and nothing else.
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

// The fixed bits of a pattern, and its slices in the order their letters first
// appear.
struct Pattern {
  static constexpr std::size_t max_slices = 4;
  static constexpr std::size_t num_bits = 8;

  std::uint8_t opcode_bits{};
  Vector<BitSlice, max_slices> slices{};
};

// Parses an eight-character pattern such as `01yyyzzz`: the fixed bits go into
// `opcode_bits`, and each distinct letter becomes a slice, whose bits must be
// contiguous.
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
      if (!result.slices.try_push_back({.name = character, .shift = bit, .mask = 1}))
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
