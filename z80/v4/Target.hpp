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

// The description, embedded. `#embed` into a braced array gives the bytes with
// no terminator, so `sizeof raw` is exactly their length; the array is local
// to the lambda so that the view is formed from it there, and only the view
// is visible.
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

  // The palettes: types built to be named, every public static function of
  // which is a verb. The chip's own verbs are the members `Z80` publishes with
  // `[[=refract::operation]]`, and its locations are whatever it can `read`,
  // so neither is listed here.
  static consteval std::vector<std::meta::info> palettes() { return {^^Operations, ^^Alu}; }
};

} // namespace specbolt::v4
