#pragma once

#ifndef SPECBOLT_IN_MODULE
#include <cstdint>
#include <string>
#endif

#include "module.hpp"

namespace specbolt {

SPECBOLT_EXPORT class Flags {
public:
  constexpr Flags() = default;
  constexpr explicit Flags(const std::uint8_t value) : value_(value) {}
  [[nodiscard]] constexpr std::uint8_t to_u8() const { return value_; }

  constexpr Flags operator&(const Flags rhs) const { return Flags(value_ & rhs.value_); }
  constexpr Flags operator|(const Flags rhs) const { return Flags(value_ | rhs.value_); }
  constexpr Flags operator^(const Flags rhs) const { return Flags(value_ ^ rhs.value_); }
  constexpr Flags operator~() const { return Flags(~value_); }

  [[nodiscard]] static constexpr Flags Carry() { return Flags(Flag::carry); }
  [[nodiscard]] static constexpr Flags Subtract() { return Flags(Flag::subtract); }
  [[nodiscard]] static constexpr Flags Parity() { return Flags(Flag::parity); }
  [[nodiscard]] static constexpr Flags Overflow() { return Flags(Flag::parity); }
  [[nodiscard]] static constexpr Flags Flag3() { return Flags(Flag::flag3); }
  [[nodiscard]] static constexpr Flags HalfCarry() { return Flags(Flag::half_carry); }
  [[nodiscard]] static constexpr Flags Flag5() { return Flags(Flag::flag5); }
  [[nodiscard]] static constexpr Flags Zero() { return Flags(Flag::zero); }
  [[nodiscard]] static constexpr Flags Sign() { return Flags(Flag::sign); }

  [[nodiscard]] constexpr bool carry() const { return value_ & Flag::carry; }
  [[nodiscard]] constexpr bool subtract() const { return value_ & Flag::subtract; }
  [[nodiscard]] constexpr bool parity() const { return value_ & Flag::parity; }
  [[nodiscard]] constexpr bool overflow() const { return value_ & Flag::parity; }
  [[nodiscard]] constexpr bool half_carry() const { return value_ & Flag::half_carry; }
  [[nodiscard]] constexpr bool zero() const { return value_ & Flag::zero; }
  [[nodiscard]] constexpr bool sign() const { return value_ & Flag::sign; }

  [[nodiscard]] std::string to_string() const;

  constexpr bool operator==(const Flags &rhs) const = default;

private:
  std::uint8_t value_{};

  struct Flag {
    static constexpr std::uint8_t carry = 0x01;
    static constexpr std::uint8_t subtract = 0x02;
    static constexpr std::uint8_t parity = 0x04;
    static constexpr std::uint8_t flag3 = 0x08;
    static constexpr std::uint8_t half_carry = 0x10;
    static constexpr std::uint8_t flag5 = 0x20;
    static constexpr std::uint8_t zero = 0x40;
    static constexpr std::uint8_t sign = 0x80;
  };
};

} // namespace specbolt

#if __has_include(<catch2/catch_tostring.hpp>)
#include <catch2/catch_tostring.hpp>
template<>
struct Catch::StringMaker<specbolt::Flags> {
  static std::string convert(const specbolt::Flags &value) { return value.to_string(); }
};
#endif
