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
  static R16 add16(std::uint16_t lhs, std::uint16_t rhs, bool carry_in);
  static R8 xor8(std::uint8_t lhs, std::uint8_t rhs);
  // add to SP alledgely updates flags, bit 11 to 12? not done in jsspeccy?
  // https://stackoverflow.com/questions/57958631/game-boy-half-carry-flag-and-16-bit-instructions-especially-opcode-0xe8
};

} // namespace specbolt
