#ifndef SPECBOLT_MODULES
#include "z80/common/Alu.hpp"

#include <bit>
#endif

namespace specbolt {

namespace {

constexpr Flags sz53_8(const std::uint8_t value) {
  return (Flags(value) & (Flags::Sign() | Flags::Flag3() | Flags::Flag5())) | (value == 0 ? Flags::Zero() : Flags());
}
constexpr Flags sz53_parity(const std::uint8_t value) {
  return (Flags(value) & (Flags::Sign() | Flags::Flag3() | Flags::Flag5())) | (value == 0 ? Flags::Zero() : Flags()) |
         (std::popcount(value) % 2 == 0 ? Flags::Parity() : Flags());
}

constexpr auto mask53 = Flags::Flag5() | Flags::Flag3();

} // namespace

Alu::R8 Alu::add8(const std::uint8_t lhs, const std::uint8_t rhs, const bool carry_in) {
  const std::uint16_t intermediate =
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(lhs) + static_cast<std::uint16_t>(rhs)) +
      static_cast<std::uint16_t>(carry_in);
  const auto carry = intermediate > 0xff ? Flags::Carry() : Flags();
  const auto result = static_cast<std::uint8_t>(intermediate);
  const auto half_carry = (lhs & 0xf) + (rhs & 0xf) + (carry_in ? 1 : 0) > 0xf ? Flags::HalfCarry() : Flags();
  const auto overflow = (lhs ^ result) & (rhs ^ result) & 0x80 ? Flags::Overflow() : Flags();
  return {result, sz53_8(result) | carry | half_carry | overflow};
}

Alu::R8 Alu::sub8(const std::uint8_t lhs, const std::uint8_t rhs, const bool carry_in) {
  const auto [result, flags] = add8(lhs, ~rhs, !carry_in);
  return {result, (flags | Flags::Subtract()) ^ Flags::Carry() ^ Flags::HalfCarry()};
}

Alu::R8 Alu::inc8(const std::uint8_t lhs, const Flags current_flags) {
  const auto result = static_cast<std::uint8_t>(lhs + 1);
  const auto flags = sz53_8(result) | (Flags(result) & mask53) | (Flags(result ^ lhs) & Flags::HalfCarry()) |
                     (result == 0x80 ? Flags::Overflow() : Flags()) | (current_flags & Flags::Carry());
  return {result, flags};
}
Alu::R8 Alu::dec8(const std::uint8_t lhs, const Flags current_flags) {
  const auto result = static_cast<std::uint8_t>(lhs - 1);
  const auto flags = Flags::Subtract() | sz53_8(result) | (Flags(result) & mask53) |
                     (result == 0x7f ? Flags::Overflow() : Flags()) | (Flags(result ^ lhs) & Flags::HalfCarry()) |
                     (current_flags & Flags::Carry());
  return {result, flags};
}

Alu::R8 Alu::cmp8(const std::uint8_t lhs, const std::uint8_t rhs) {
  const auto [result, flags] = sub8(lhs, rhs, false);
  // Per http://www.z80.info/z80sflag.htm; F5 and F3 are copied from the operand, not the result
  return {lhs, (flags & ~mask53) | (Flags(rhs) & mask53)};
}

Alu::R16 Alu::add16(const std::uint16_t lhs, const std::uint16_t rhs, const Flags current_flags) {
  const std::uint32_t intermediate = lhs + rhs;
  const auto carry = intermediate > 0xffff ? Flags::Carry() : Flags();
  const auto result = static_cast<std::uint16_t>(intermediate);
  const auto half_carry = (lhs & 0xfff) + (rhs & 0xfff) > 0xfff ? Flags::HalfCarry() : Flags();
  const auto flags35 = Flags(static_cast<std::uint8_t>(result >> 8u)) & (Flags::Flag5() | Flags::Flag3());
  return {result, (current_flags & (Flags::Sign() | Flags::Zero() | Flags::Parity())) | carry | half_carry | flags35};
}

Alu::R16 Alu::sub16(const std::uint16_t lhs, const std::uint16_t rhs, const Flags current_flags) {
  const auto [result, flags] = add16(lhs, ~rhs, current_flags);
  return {result, (flags | Flags::Subtract()) ^ Flags::Carry() ^ Flags::HalfCarry()};
}

Alu::R16 Alu::adc16(const std::uint16_t lhs, const std::uint16_t rhs, const bool carry_in) {
  const std::uint32_t intermediate = lhs + rhs + (carry_in ? 1 : 0);
  const auto carry = intermediate > 0xffff ? Flags::Carry() : Flags();
  const auto result = static_cast<std::uint16_t>(intermediate);
  const auto half_carry = (lhs & 0xfff) + (rhs & 0xfff) > 0xfff ? Flags::HalfCarry() : Flags();
  const auto flags35 = Flags(static_cast<std::uint8_t>(result >> 8u)) & (Flags::Flag5() | Flags::Flag3());
  const auto negative = result & 0x8000 ? Flags::Sign() : Flags();
  const auto zero = result == 0 ? Flags::Zero() : Flags();
  const auto overflow = (lhs ^ result) & (rhs ^ result) & 0x8000 ? Flags::Overflow() : Flags();
  return {result, carry | half_carry | flags35 | negative | zero | overflow};
}

Alu::R16 Alu::sbc16(const std::uint16_t lhs, const std::uint16_t rhs, const bool carry_in) {
  const auto [result, flags] = adc16(lhs, ~rhs, !carry_in);
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

Alu::R8 Alu::daa(const std::uint8_t lhs, const Flags current_flags) {
  const auto sign_adjust = current_flags.subtract() ? -1 : 1;
  const auto low_nibble_carry = (lhs & 0xf) > 0x09 || current_flags.half_carry();
  const auto high_nibble_carry = lhs > 0x99 || current_flags.carry();
  const auto result = static_cast<std::uint8_t>(
      lhs + (low_nibble_carry ? 0x06 * sign_adjust : 0) + (high_nibble_carry ? 0x60 * sign_adjust : 0));
  const auto carry = lhs > 0x99 ? Flags::Carry() : Flags();
  const auto half_carry = (lhs ^ result) & 0x10 ? Flags::HalfCarry() : Flags();
  const auto preserved_flags = current_flags & (Flags::Carry() | Flags::Subtract());
  return {result, sz53_parity(result) | carry | half_carry | preserved_flags};
}

Alu::R8 Alu::cpl(const std::uint8_t lhs, const Flags current_flags) {
  const auto result = static_cast<std::uint8_t>(lhs ^ 0xff);
  const auto preserved_flags = current_flags & (Flags::Sign() | Flags::Zero() | Flags::Parity() | Flags::Carry());
  constexpr auto set_flags = Flags::HalfCarry() | Flags::Subtract();
  const auto flags_from_result = Flags(result) & (Flags::Flag3() | Flags::Flag5());
  return {result, preserved_flags | set_flags | flags_from_result};
}

Alu::R8 Alu::scf(const std::uint8_t lhs, const Flags current_flags) {
  const auto preserved = current_flags & (Flags::Sign() | Flags::Zero() | Flags::Parity());
  const auto flags53 = Flags(lhs) & (Flags::Flag3() | Flags::Flag5());
  return {lhs, preserved | flags53 | Flags::Carry()};
}

Alu::R8 Alu::ccf(const std::uint8_t lhs, const Flags current_flags) {
  const auto preserved = current_flags & (Flags::Sign() | Flags::Zero() | Flags::Parity() | Flags::Carry());
  const auto flags53 = Flags(lhs) & (Flags::Flag3() | Flags::Flag5());
  const auto other_flag = current_flags.carry() ? Flags::HalfCarry() : Flags();
  return {lhs, (preserved | flags53 | other_flag) ^ Flags::Carry()};
}

Flags Alu::bit(
    const std::uint8_t lhs, const std::uint8_t rhs, const Flags current_flags, const std::uint8_t bus_noise) {
  const auto flags_persisted = current_flags & Flags::Carry();
  const auto flags_from_value = Flags(bus_noise) & (Flags::Flag3() | Flags::Flag5());
  const auto flags_from_sign = (rhs & lhs & 0x80) ? Flags::Sign() : Flags();
  const auto flags_from_bit = lhs & rhs ? Flags() : (Flags::Zero() | Flags::Parity());
  return flags_persisted | flags_from_value | flags_from_sign | flags_from_bit | Flags::HalfCarry();
}

Flags Alu::parity_flags_for(const std::uint8_t value) { return sz53_parity(value); }

Flags Alu::iff2_flags_for(const std::uint8_t value, const Flags current_flags, const bool iff2) {
  const auto flags_persisted = current_flags & Flags::Carry();
  const auto flags_from_value = Flags(value) & (Flags::Flag3() | Flags::Flag5());
  const auto flags_from_iff = iff2 ? Flags::Parity() : Flags();
  return sz53_8(value) | flags_persisted | flags_from_value | flags_from_iff;
}

Alu::R8 Alu::fast_rotate8(const std::uint8_t lhs, const Direction direction, const Flags flags) {
  const auto [result, result_flags] = rotate8(lhs, direction, flags.carry());
  constexpr auto preserved_original_flags = Flags::Sign() | Flags::Zero() | Flags::Parity();
  constexpr auto preserved_result_flags = Flags::Carry() | Flags::Flag3() | Flags::Flag5();
  return {result, (flags & preserved_original_flags) | (result_flags & preserved_result_flags)};
}

Alu::R8 Alu::fast_rotate_circular8(const std::uint8_t lhs, const Direction direction, const Flags flags) {
  const auto [result, result_flags] = rotate_circular8(lhs, direction);
  constexpr auto preserved_original_flags = Flags::Sign() | Flags::Zero() | Flags::Parity();
  constexpr auto preserved_result_flags = Flags::Carry() | Flags::Flag3() | Flags::Flag5();
  return {result, (flags & preserved_original_flags) | (result_flags & preserved_result_flags)};
}

Alu::R8 Alu::and8(const std::uint8_t lhs, const std::uint8_t rhs) {
  const auto result = static_cast<std::uint8_t>(lhs & rhs);
  return {result, sz53_parity(result) | Flags::HalfCarry()};
}

Alu::R8 Alu::rotate8(const std::uint8_t lhs, const Direction direction, const bool carry_in) {
  const bool carry_out = direction == Direction::Left ? lhs & 0x80 : lhs & 1;
  const auto result = direction == Direction::Left ? static_cast<std::uint8_t>(lhs << 1 | carry_in)
                                                   : static_cast<std::uint8_t>(lhs >> 1 | (carry_in ? 0x80 : 0));
  return {result, sz53_parity(result) | (carry_out ? Flags::Carry() : Flags())};
}

Alu::R8 Alu::rotate_circular8(const std::uint8_t lhs, const Direction direction) {
  const bool carry_out = direction == Direction::Left ? lhs & 0x80 : lhs & 1;
  const auto result = direction == Direction::Left ? static_cast<std::uint8_t>(lhs << 1 | (carry_out ? 0x01 : 0x00))
                                                   : static_cast<std::uint8_t>(lhs >> 1 | (carry_out ? 0x80 : 0x00));
  return {result, sz53_parity(result) | (carry_out ? Flags::Carry() : Flags())};
}

Alu::R8 Alu::shift_logical8(const std::uint8_t lhs, const Direction direction) {
  const bool carry_out = direction == Direction::Left ? lhs & 0x80 : lhs & 1;
  const auto result =
      direction == Direction::Left ? static_cast<std::uint8_t>(lhs << 1 | 1) : static_cast<std::uint8_t>(lhs >> 1);
  return {result, sz53_parity(result) | (carry_out ? Flags::Carry() : Flags())};
}

Alu::R8 Alu::shift_arithmetic8(const std::uint8_t lhs, const Direction direction) {
  const bool carry_out = direction == Direction::Left ? lhs & 0x80 : lhs & 1;
  const auto result = direction == Direction::Left ? static_cast<std::uint8_t>(lhs << 1)
                                                   : static_cast<std::uint8_t>(lhs >> 1 | (lhs & 0x80));
  return {result, sz53_parity(result) | (carry_out ? Flags::Carry() : Flags())};
}

} // namespace specbolt
