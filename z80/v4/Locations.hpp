#pragma once

// Where a row's names for storage are looked up.
//
// The registers come from `RegisterFile`; the rest are the enums `Z80.hpp`
// declares alongside the `read` and `write` overloads that reach them. All this
// file adds is the list: the scopes reflection is to search when a row says `a`
// or `carry` or `pc`, in the order it is to search them.

#include "z80/common/RegisterFile.hpp"
#include "z80/v4/Z80.hpp"

#include <array>
#include <meta>

namespace specbolt::v4 {

// Where the table may name storage locations from.
[[nodiscard]] consteval std::array<std::meta::info, 9> location_scopes() {
  return {^^RegisterFile::R8, ^^RegisterFile::R16, ^^FlagBit, ^^FlagWord, ^^FlipFlop, ^^ProgramCounter, ^^AddressLatch,
      ^^Interrupt, ^^Refresh};
}

} // namespace specbolt::v4
