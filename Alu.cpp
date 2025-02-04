#include "Alu.hpp"

#include <array>

namespace specbolt {

namespace {

constexpr std::array<Flags, 256> parity_table = [] {
  std::array<Flags, 256> table{};
  for (std::size_t i = 0; i < 256; ++i)
    table[i] = std::popcount(i) % 2 == 0 ? Flags::Parity() : Flags{};
  return table;
}();

constexpr std::array<Flags, 256> sz53_table = [] {
  std::array<Flags, 256> table{};
  for (std::size_t i = 0; i < 256; ++i)
    table[i] = Flags(static_cast<std::uint8_t>(i)) & (Flags::Sign() | Flags::Flag3() | Flags::Flag5()) |
               (i == 0 ? Flags::Zero() : Flags());
  return table;
}();

constexpr std::array<Flags, 256> sz53_parity_table = [] {
  std::array<Flags, 256> table{};
  for (std::size_t i = 0; i < 256; ++i)
    table[i] = parity_table[i] | sz53_table[i];
  return table;
}();

} // namespace

Alu::Result Alu::add8(const std::uint8_t lhs, const std::uint8_t rhs, const bool carry_in) {
  const std::uint16_t intermediate = lhs + rhs + (carry_in ? 1 : 0);
  const auto carry = intermediate > 0xff ? Flags::Carry() : Flags();
  const auto result = static_cast<std::uint8_t>(intermediate);
  const auto half_carry = (lhs & 0xf) + (rhs & 0xf) + (carry_in ? 1 : 0) > 0xf ? Flags::HalfCarry() : Flags();
  const auto overflow = (lhs ^ result) & (rhs ^ result) & 0x80 ? Flags::Overflow() : Flags();
  return {result, sz53_table[result] | carry | half_carry | overflow};
}

Alu::Result Alu::xor8(const std::uint8_t lhs, const std::uint8_t rhs) {
  const auto result = static_cast<std::uint8_t>(lhs ^ rhs);
  return {result, sz53_parity_table[result]};
}

} // namespace specbolt
