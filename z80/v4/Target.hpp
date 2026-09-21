#pragma once

// What refract generates for: the Z80, the verbs its description may name, and
// the description itself. This is the whole of the Z80's side of the contract
// with the library; a second machine would be a second one of these.

#include "Operations.hpp"
#include "refract/Compiled.hpp"
#include "z80/common/Alu.hpp"
#include "z80/v4/Z80.hpp"

#include <meta>
#include <string_view>
#include <vector>

namespace specbolt::v4 {

// The description, embedded. The bytes are local to the lambda so that only
// the view of them is visible.
inline constexpr std::string_view z80_cpu = [] {
  // clang-format off
  static constexpr char raw[] = {
#embed "z80.cpu"
  };
  // clang-format on
  return std::string_view{raw, sizeof raw};
}();

struct Target {
  using Machine = Z80;
  using Compiled = refract::Compiled<z80_cpu, "z80.cpu">;

  // Where the description's operations may come from. Where its *locations*
  // come from is not stated: a location is a thing the machine can read, so
  // refract derives them from the machine's `read` overloads.
  static consteval std::vector<std::meta::info> operation_scopes() { return {^^Operations, ^^Alu}; }
};

} // namespace specbolt::v4
