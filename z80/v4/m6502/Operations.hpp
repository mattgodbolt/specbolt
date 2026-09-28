#pragma once

// The 6502 operations that touch nothing but their arguments: the palette its target lists. Status bits are passed and
// returned as the whole `p` byte.

#include "refract/Continue.hpp"

#include <cstdint>

namespace specbolt::m6502 {

// A value and the status it leaves, which a row writes to two destinations: `a, p <- ...`.
struct Loaded {
  std::uint8_t value;
  std::uint8_t p;
};

struct Operations {
  static constexpr std::uint8_t carry = 0x01;
  static constexpr std::uint8_t zero = 0x02;
  static constexpr std::uint8_t overflow = 0x40;
  static constexpr std::uint8_t negative = 0x80;

  // `p` with negative and zero set from `value`, as nearly every instruction that produces a value leaves them.
  [[nodiscard]] static constexpr std::uint8_t nz(const std::uint8_t p, const std::uint8_t value) {
    return static_cast<std::uint8_t>((p & ~(negative | zero)) | (value & negative) | (value == 0 ? zero : 0));
  }
  [[nodiscard]] static constexpr std::uint8_t with(const std::uint8_t p, const std::uint8_t bits, const bool on) {
    return static_cast<std::uint8_t>(on ? p | bits : p & ~bits);
  }

  // Moves a value without touching the status.
  [[nodiscard]] static constexpr std::uint8_t ld8(const std::uint8_t value) { return value; }
  [[nodiscard]] static constexpr std::uint16_t ld16(const std::uint16_t value) { return value; }
  [[nodiscard]] static constexpr std::uint16_t inc16(const std::uint16_t value) {
    return static_cast<std::uint16_t>(value + 1);
  }
  [[nodiscard]] static constexpr std::uint16_t dec16(const std::uint16_t value) {
    return static_cast<std::uint16_t>(value - 1);
  }
  // Moves a value and sets negative and zero from it: `lda`, `ldx`, `tax` and the rest.
  [[nodiscard]] static constexpr Loaded load8(const std::uint8_t value, const std::uint8_t p) {
    return {value, nz(p, value)};
  }

  // The accumulator group but `sta`. Every one takes the accumulator, the operand and the status, and returns the
  // accumulator and the status, so that one row can name any of them; `lda` ignores the accumulator it is given and
  // `cmp` hands it back unchanged. Decimal mode is not modelled.
  [[nodiscard]] static constexpr Loaded ora8(const std::uint8_t a, const std::uint8_t value, const std::uint8_t p) {
    return load8(a | value, p);
  }
  [[nodiscard]] static constexpr Loaded and8(const std::uint8_t a, const std::uint8_t value, const std::uint8_t p) {
    return load8(a & value, p);
  }
  [[nodiscard]] static constexpr Loaded eor8(const std::uint8_t a, const std::uint8_t value, const std::uint8_t p) {
    return load8(a ^ value, p);
  }
  [[nodiscard]] static constexpr Loaded adc8(const std::uint8_t a, const std::uint8_t value, const std::uint8_t p) {
    const unsigned sum = a + value + (p & carry);
    const auto result = static_cast<std::uint8_t>(sum);
    auto status = with(nz(p, result), carry, sum > 0xff);
    status = with(status, overflow, (~(a ^ value) & (a ^ result) & 0x80) != 0);
    return {result, status};
  }
  [[nodiscard]] static constexpr Loaded sbc8(const std::uint8_t a, const std::uint8_t value, const std::uint8_t p) {
    return adc8(a, static_cast<std::uint8_t>(~value), p);
  }
  [[nodiscard]] static constexpr Loaded lda8(const std::uint8_t, const std::uint8_t value, const std::uint8_t p) {
    return load8(value, p);
  }
  // Also `cpx` and `cpy`: the register is handed back as it was.
  [[nodiscard]] static constexpr Loaded cmp8(const std::uint8_t reg, const std::uint8_t value, const std::uint8_t p) {
    const auto difference = static_cast<std::uint8_t>(reg - value);
    return {reg, with(nz(p, difference), carry, reg >= value)};
  }
  // `bit`: zero from the accumulator masked by the operand, and negative and overflow straight from the operand.
  [[nodiscard]] static constexpr std::uint8_t bit8(
      const std::uint8_t a, const std::uint8_t value, const std::uint8_t p) {
    const auto status = with(p, zero, (a & value) == 0);
    return static_cast<std::uint8_t>((status & ~(negative | overflow)) | (value & (negative | overflow)));
  }

  // The shifts and the increments, which read a value and write it back.
  [[nodiscard]] static constexpr Loaded asl8(const std::uint8_t value, const std::uint8_t p) {
    return {
        static_cast<std::uint8_t>(value << 1), with(nz(p, static_cast<std::uint8_t>(value << 1)), carry, value & 0x80)};
  }
  [[nodiscard]] static constexpr Loaded lsr8(const std::uint8_t value, const std::uint8_t p) {
    return {
        static_cast<std::uint8_t>(value >> 1), with(nz(p, static_cast<std::uint8_t>(value >> 1)), carry, value & 1)};
  }
  [[nodiscard]] static constexpr Loaded rol8(const std::uint8_t value, const std::uint8_t p) {
    const auto result = static_cast<std::uint8_t>(value << 1 | (p & carry));
    return {result, with(nz(p, result), carry, value & 0x80)};
  }
  [[nodiscard]] static constexpr Loaded ror8(const std::uint8_t value, const std::uint8_t p) {
    const auto result = static_cast<std::uint8_t>(value >> 1 | (p & carry) << 7);
    return {result, with(nz(p, result), carry, value & 1)};
  }
  [[nodiscard]] static constexpr Loaded inc8(const std::uint8_t value, const std::uint8_t p) {
    return load8(static_cast<std::uint8_t>(value + 1), p);
  }
  [[nodiscard]] static constexpr Loaded dec8(const std::uint8_t value, const std::uint8_t p) {
    return load8(static_cast<std::uint8_t>(value - 1), p);
  }

  // `clc`, `sec` and the rest: one status bit set or cleared.
  [[nodiscard]] static constexpr std::uint8_t flag(
      const std::uint8_t p, const std::uint8_t bits, const std::uint8_t on) {
    return with(p, bits, on != 0);
  }

  // The branch conditions: one status bit, clear or set.
  [[nodiscard]] static constexpr refract::Continue bit_clear(const std::uint8_t p, const std::uint8_t bits) {
    return refract::continue_if((p & bits) == 0);
  }
  [[nodiscard]] static constexpr refract::Continue bit_set(const std::uint8_t p, const std::uint8_t bits) {
    return refract::continue_if((p & bits) != 0);
  }
};

} // namespace specbolt::m6502
