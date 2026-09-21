#pragma once

// The verbs of the description that touch no chip: a palette, every public
// static function of which a row may name. The verbs that do touch the chip
// are members of `Z80`, marked there one by one; the arithmetic the rows
// share with the other implementations is in `Alu`, a second palette.

#include "z80/common/Alu.hpp"
#include "z80/common/Flags.hpp"

#include <cstdint>

namespace specbolt::v4 {

struct Operations {
  static constexpr void nop() {}
  [[nodiscard]] static constexpr std::uint8_t ld8(const std::uint8_t value) { return value; }
  [[nodiscard]] static constexpr std::uint16_t ld16(const std::uint16_t value) { return value; }
  [[nodiscard]] static constexpr std::uint16_t inc16(const std::uint16_t value) {
    return static_cast<std::uint16_t>(value + 1);
  }
  [[nodiscard]] static constexpr std::uint16_t dec16(const std::uint16_t value) {
    return static_cast<std::uint16_t>(value - 1);
  }

  // `Alu::bit` takes a mask; the encoding carries an index, as `res` and `set`
  // do. Flags 3 and 5 come from whatever was last on the bus, which the row
  // names. Not `bit`, because `Alu::bit` is in the same search and the two
  // would be ambiguous.
  [[nodiscard]] static constexpr Flags test_bit(
      const std::uint8_t value, const std::uint8_t bit, const Flags flags, const std::uint8_t bus) {
    return Alu::bit(value, static_cast<std::uint8_t>(1u << bit), flags, bus);
  }
  [[nodiscard]] static constexpr std::uint8_t res(const std::uint8_t value, const std::uint8_t bit) {
    return static_cast<std::uint8_t>(value & ~(1u << bit));
  }
  [[nodiscard]] static constexpr std::uint8_t set(const std::uint8_t value, const std::uint8_t bit) {
    return static_cast<std::uint8_t>(value | 1u << bit);
  }

  // Conditions: an `if` step applies one of these and abandons the rest of the
  // row when it answers false. A vocabulary member binds one and appends the
  // flag bit it asks about, as `nz:is_clear(zero)` does.
  [[nodiscard]] static constexpr bool is_set(const bool flag) { return flag; }
  [[nodiscard]] static constexpr bool is_clear(const bool flag) { return !flag; }
  [[nodiscard]] static constexpr bool nonzero(const std::uint8_t value) { return value != 0; }
  [[nodiscard]] static constexpr bool nonzero16(const std::uint16_t value) { return value != 0; }

  // `Alu::dec8` sets the flags; `djnz` counts without touching them.
  [[nodiscard]] static constexpr std::uint8_t dec8_no_flags(const std::uint8_t value) {
    return static_cast<std::uint8_t>(value - 1);
  }

  [[nodiscard]] static constexpr std::uint16_t relative(const std::uint16_t base, const std::uint8_t offset) {
    return static_cast<std::uint16_t>(base + static_cast<std::int8_t>(offset));
  }

  // `neg` is `0 - a`, which sub8 already is.
  [[nodiscard]] static constexpr Alu::R8 neg8(const std::uint8_t value) { return Alu::sub8(0, value, false); }
};

} // namespace specbolt::v4
