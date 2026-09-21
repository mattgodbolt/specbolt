#pragma once

#ifndef SPECBOLT_MODULES
#include <cstdint>
#include <string>
#include <utility>
#endif

namespace specbolt {

SPECBOLT_EXPORT
class Flags {
public:
  // One flag each, as the bit it occupies in the register, so that a single
  // flag can be spoken of on its own.
  enum class Bit : std::uint8_t {
    carry = 0x01,
    subtract = 0x02,
    parity = 0x04,
    flag3 = 0x08,
    half_carry = 0x10,
    flag5 = 0x20,
    zero = 0x40,
    sign = 0x80,
  };

  constexpr Flags() = default;
  constexpr explicit Flags(const std::uint8_t value) : value_(value) {}
  constexpr explicit Flags(const Bit bit) : value_(std::to_underlying(bit)) {}
  [[nodiscard]] constexpr std::uint8_t to_u8() const { return value_; }

  [[nodiscard]] constexpr bool test(const Bit bit) const { return (value_ & std::to_underlying(bit)) != 0; }

  constexpr Flags operator&(const Flags rhs) const { return Flags(value_ & rhs.value_); }
  constexpr Flags operator|(const Flags rhs) const { return Flags(value_ | rhs.value_); }
  constexpr Flags operator^(const Flags rhs) const { return Flags(value_ ^ rhs.value_); }
  constexpr Flags operator~() const { return Flags(static_cast<std::uint8_t>(~value_)); }

  [[nodiscard]] static constexpr Flags Carry() { return Flags(Bit::carry); }
  [[nodiscard]] static constexpr Flags Subtract() { return Flags(Bit::subtract); }
  [[nodiscard]] static constexpr Flags Parity() { return Flags(Bit::parity); }
  [[nodiscard]] static constexpr Flags Overflow() { return Flags(Bit::parity); }
  [[nodiscard]] static constexpr Flags Flag3() { return Flags(Bit::flag3); }
  [[nodiscard]] static constexpr Flags HalfCarry() { return Flags(Bit::half_carry); }
  [[nodiscard]] static constexpr Flags Flag5() { return Flags(Bit::flag5); }
  [[nodiscard]] static constexpr Flags Zero() { return Flags(Bit::zero); }
  [[nodiscard]] static constexpr Flags Sign() { return Flags(Bit::sign); }

  [[nodiscard]] constexpr bool carry() const { return test(Bit::carry); }
  [[nodiscard]] constexpr bool subtract() const { return test(Bit::subtract); }
  [[nodiscard]] constexpr bool parity() const { return test(Bit::parity); }
  [[nodiscard]] constexpr bool overflow() const { return test(Bit::parity); }
  [[nodiscard]] constexpr bool half_carry() const { return test(Bit::half_carry); }
  [[nodiscard]] constexpr bool zero() const { return test(Bit::zero); }
  [[nodiscard]] constexpr bool sign() const { return test(Bit::sign); }

  [[nodiscard]] std::string to_string() const;

  constexpr bool operator==(const Flags &rhs) const = default;

  // Private for the usual reason, with the usual consequence: a type whose
  // state is its own cannot be decomposed, so `Flags` is one value everywhere,
  // including anywhere that would otherwise take it apart.
private:
  std::uint8_t value_{};
};

} // namespace specbolt

#if __has_include(<catch2/catch_tostring.hpp>)
#include <catch2/catch_tostring.hpp>
template<>
struct Catch::StringMaker<specbolt::Flags> {
  static std::string convert(const specbolt::Flags &value) { return value.to_string(); }
};
#endif
