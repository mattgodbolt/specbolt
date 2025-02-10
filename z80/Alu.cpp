#include "z80/Alu.hpp"
#include <bit>

namespace specbolt {

namespace {

constexpr Flags sz53_8(const std::uint8_t value) {
  return Flags(value) & (Flags::Sign() | Flags::Flag3() | Flags::Flag5()) | (value == 0 ? Flags::Zero() : Flags());
}
constexpr Flags sz53_parity(const std::uint8_t value) {
  return Flags(value) & (Flags::Sign() | Flags::Flag3() | Flags::Flag5()) | (value == 0 ? Flags::Zero() : Flags()) |
         (std::popcount(value) % 2 == 0 ? Flags::Parity() : Flags());
}
constexpr Flags sz53_16(const std::uint16_t value) {
  return Flags(value >> 8) & (Flags::Sign() | Flags::Flag3() | Flags::Flag5()) | (value == 0 ? Flags::Zero() : Flags());
}

} // namespace

Alu::R8 Alu::add8(const std::uint8_t lhs, const std::uint8_t rhs, const bool carry_in) {
  const std::uint16_t intermediate = lhs + rhs + (carry_in ? 1 : 0);
  const auto carry = intermediate > 0xff ? Flags::Carry() : Flags();
  const auto result = static_cast<std::uint8_t>(intermediate);
  const auto half_carry = (lhs & 0xf) + (rhs & 0xf) + (carry_in ? 1 : 0) > 0xf ? Flags::HalfCarry() : Flags();
  const auto overflow = (lhs ^ result) & (rhs ^ result) & 0x80 ? Flags::Overflow() : Flags();
  return {result, sz53_8(result) | carry | half_carry | overflow};
}

Alu::R8 Alu::sub8(const std::uint8_t lhs, const std::uint8_t rhs, const bool carry_in) {
  const auto result = add8(lhs, ~rhs, !carry_in);
  return {result.result, (result.flags | Flags::Subtract()) ^ Flags::Carry() ^ Flags::HalfCarry()};
}

Alu::R8 Alu::cmp8(const std::uint8_t lhs, const std::uint8_t rhs) {
  const auto result = sub8(lhs, rhs, false);
  // Per http://www.z80.info/z80sflag.htm; F5 and F3 are copied from the operand, not the result
  const auto mask53 = Flags::Flag5() | Flags::Flag3();
  return {result.result, result.flags & ~mask53 | Flags(lhs) & mask53};
}

Alu::R16 Alu::add16(const std::uint16_t lhs, const std::uint16_t rhs, const bool carry_in) {
  const std::uint32_t intermediate = lhs + rhs + (carry_in ? 1 : 0);
  const auto carry = intermediate > 0xffff ? Flags::Carry() : Flags();
  const auto result = static_cast<std::uint16_t>(intermediate);
  const auto half_carry = (lhs & 0xf00) + (rhs & 0xf00) > 0xf00 ? Flags::HalfCarry() : Flags();
  const auto overflow = (lhs ^ result) & (rhs ^ result) & 0x8000 ? Flags::Overflow() : Flags();
  return {result, sz53_16(result) | carry | half_carry | overflow};
}

Alu::R16 Alu::sub16(const std::uint16_t lhs, const std::uint16_t rhs, const bool carry_in) {
  const auto result = add16(lhs, ~rhs, !carry_in);
  return {result.result, (result.flags | Flags::Subtract()) ^ Flags::Carry() ^ Flags::HalfCarry()};
}

Alu::R8 Alu::xor8(const std::uint8_t lhs, const std::uint8_t rhs) {
  const auto result = static_cast<std::uint8_t>(lhs ^ rhs);
  return {result, sz53_parity(result)};
}
Alu::R8 Alu::or8(const std::uint8_t lhs, const std::uint8_t rhs) {
  const auto result = static_cast<std::uint8_t>(lhs | rhs);
  return {result, sz53_parity(result)};
}
Alu::R8 Alu::and8(const std::uint8_t lhs, const std::uint8_t rhs) {
  const auto result = static_cast<std::uint8_t>(lhs & rhs);
  return {result, sz53_parity(result)};
}

} // namespace specbolt
