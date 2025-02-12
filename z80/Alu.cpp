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
  const auto [result, flags] = sub8(lhs, rhs, false);
  // Per http://www.z80.info/z80sflag.htm; F5 and F3 are copied from the operand, not the result
  constexpr auto mask53 = Flags::Flag5() | Flags::Flag3();
  return {result, flags & ~mask53 | Flags(lhs) & mask53};
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
  const auto [result, flags] = add16(lhs, ~rhs, !carry_in);
  return {result, (flags | Flags::Subtract()) ^ Flags::Carry() ^ Flags::HalfCarry()};
}

Alu::R8 Alu::xor8(const std::uint8_t lhs, const std::uint8_t rhs) {
  const auto result = static_cast<std::uint8_t>(lhs ^ rhs);
  return {result, sz53_parity(result)};
}

Alu::R8 Alu::or8(const std::uint8_t lhs, const std::uint8_t rhs) {
  const auto result = static_cast<std::uint8_t>(lhs | rhs);
  return {result, sz53_parity(result)};
}

Alu::R8 Alu::fast_rotate8(const std::uint8_t lhs, const Direction direction, const Flags flags) {
  const auto [result, result_flags] = rotate8(lhs, direction, flags.carry());
  return {result, flags & ~Flags::Carry() | (result_flags & Flags::Carry())};
}

Alu::R8 Alu::fast_rotate_circular8(const std::uint8_t lhs, const Direction direction, const Flags flags) {
  const auto [result, result_flags] = rotate_circular8(lhs, direction);
  return {result, flags & ~Flags::Carry() | (result_flags & Flags::Carry())};
}

Alu::R8 Alu::and8(const std::uint8_t lhs, const std::uint8_t rhs) {
  const auto result = static_cast<std::uint8_t>(lhs & rhs);
  return {result, sz53_parity(result)};
}

Alu::R8 Alu::rotate8(const std::uint8_t lhs, const Direction direction, const bool carry_in) {
  const bool carry_out = direction == Direction::Left ? lhs & 0x80 : lhs & 1;
  const auto result = direction == Direction::Left ? static_cast<std::uint8_t>(lhs << 1 | carry_in)
                                                   : static_cast<std::uint8_t>(lhs >> 1 | (carry_in ? 0x80 : 0));
  return {result, sz53_8(result) | (carry_out ? Flags::Carry() : Flags())};
}

Alu::R8 Alu::rotate_circular8(const std::uint8_t lhs, const Direction direction) {
  const bool carry_out = direction == Direction::Left ? lhs & 0x80 : lhs & 1;
  const auto result = direction == Direction::Left ? static_cast<std::uint8_t>(lhs << 1 | (carry_out ? 0x01 : 0x00))
                                                   : static_cast<std::uint8_t>(lhs >> 1 | (carry_out ? 0x80 : 0x00));
  return {result, sz53_8(result) | (carry_out ? Flags::Carry() : Flags())};
}

Alu::R8 Alu::shift_logical8(const std::uint8_t lhs, const Direction direction) {
  const bool carry_out = direction == Direction::Left ? lhs & 0x80 : lhs & 1;
  const auto result =
      direction == Direction::Left ? static_cast<std::uint8_t>(lhs << 1) : static_cast<std::uint8_t>(lhs >> 1);
  return {result, sz53_8(result) | (carry_out ? Flags::Carry() : Flags())};
}

Alu::R8 Alu::shift_arithmetic8(const std::uint8_t lhs, const Direction direction) {
  const bool carry_out = direction == Direction::Left ? lhs & 0x80 : lhs & 1;
  const auto result = direction == Direction::Left ? static_cast<std::uint8_t>(lhs << 1)
                                                   : static_cast<std::uint8_t>(lhs >> 1 | (lhs & 0x80));
  return {result, sz53_8(result) | (carry_out ? Flags::Carry() : Flags())};
}

} // namespace specbolt
