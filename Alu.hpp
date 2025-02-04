#pragma once

#include <cstdint>

#include "Flags.hpp"

namespace specbolt {

class Alu {
public:
  struct Result {
    std::uint8_t result;
    Flags flags;
    constexpr bool operator==(const Result &rhs) const = default;
  };
  static Result add8(std::uint8_t lhs, std::uint8_t rhs, bool carry_in);
  static Result xor8(std::uint8_t lhs, std::uint8_t rhs);
  // add to SP alledgely updates flags, bit 11 to 12? not done in jsspeccy?
  // https://stackoverflow.com/questions/57958631/game-boy-half-carry-flag-and-16-bit-instructions-especially-opcode-0xe8
};

} // namespace specbolt
