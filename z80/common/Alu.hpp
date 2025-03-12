#pragma once

#include <cstdint>

#include "Flags.hpp"

namespace specbolt {

class Alu {
public:
  template<typename ResultType>
  struct ResultT {
    ResultType result;
    Flags flags;
    constexpr bool operator==(const ResultT &rhs) const = default;
  };
  using R8 = ResultT<std::uint8_t>;
  using R16 = ResultT<std::uint16_t>;
  static R8 add8(std::uint8_t lhs, std::uint8_t rhs, bool carry_in);
  static R8 sub8(std::uint8_t lhs, std::uint8_t rhs, bool carry_in);
  static R8 inc8(std::uint8_t lhs, Flags current_flags);
  static R8 dec8(std::uint8_t lhs, Flags current_flags);
  static R8 cmp8(std::uint8_t lhs, std::uint8_t rhs);
  static R16 add16(std::uint16_t lhs, std::uint16_t rhs, Flags current_flags);
  static R16 sub16(std::uint16_t lhs, std::uint16_t rhs, Flags current_flags);
  static R16 adc16(std::uint16_t lhs, std::uint16_t rhs, bool carry_in);
  static R16 sbc16(std::uint16_t lhs, std::uint16_t rhs, bool carry_in);
  static R8 and8(std::uint8_t lhs, std::uint8_t rhs);
  static R8 xor8(std::uint8_t lhs, std::uint8_t rhs);
  static R8 or8(std::uint8_t lhs, std::uint8_t rhs);
  // add to SP allegedly updates flags, bit 11 to 12? not done in jsspeccy?
  // https://stackoverflow.com/questions/57958631/game-boy-half-carry-flag-and-16-bit-instructions-especially-opcode-0xe8

  static R8 daa(std::uint8_t lhs, Flags current_flags);
  static R8 cpl(std::uint8_t lhs, Flags current_flags);
  static R8 scf(std::uint8_t lhs, Flags current_flags);
  static R8 ccf(std::uint8_t lhs, Flags current_flags);
  static R8 rrd(std::uint8_t lhs, Flags current_flags);
  static R8 rld(std::uint8_t lhs, Flags current_flags);

  static Flags bit(std::uint8_t lhs, std::uint8_t rhs, Flags current_flags, std::uint8_t bus_noise);

  static Flags parity_flags_for(std::uint8_t value);
  static Flags iff2_flags_for(std::uint8_t value, Flags current_flags, bool iff2);

  enum class Direction { Left, Right };

  // "Fast" here means the flags aren't properly calculated, as in RLCA, RRCA etc.
  // Fast rotate normally.
  static R8 fast_rotate8(std::uint8_t lhs, Direction direction, Flags flags);
  // Fast rotate circularly.
  static R8 fast_rotate_circular8(std::uint8_t lhs, Direction direction, Flags flags);

  // Rotate through the carry flag.
  static R8 rotate8(std::uint8_t lhs, Direction direction, bool carry_in);
  // Rotate circularly.
  static R8 rotate_circular8(std::uint8_t lhs, Direction direction);
  static R8 shift_logical8(std::uint8_t lhs, Direction direction);
  static R8 shift_arithmetic8(std::uint8_t lhs, Direction direction);
};

} // namespace specbolt
