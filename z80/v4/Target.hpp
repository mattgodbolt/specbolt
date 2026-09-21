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

// The description: the file, embedded, and what diagnostics call it. `#embed` into a braced array gives the bytes with
// no terminator, so `sizeof raw` is exactly their length; the array is local to the lambda so that the view is formed
// from it there, and only the view is visible.
struct Z80Source {
  static constexpr std::string_view file = "z80.cpu";
  static constexpr std::string_view text = [] {
    // clang-format off
    static constexpr char raw[] = {
#embed "z80.cpu"
    };
    // clang-format on
    return std::string_view{raw, sizeof raw};
  }();
};

// What `refract::Interpreter` is instantiated on: the machine, its compiled
// description, and the palettes. `TargetLike` in refract/Machine.hpp is the
// contract this meets.
struct Target {
  using Machine = Z80;
  using Compiled = refract::Compiled<Z80Source>;

  // The palettes: types built to be named, every public static function of
  // which is a verb. The chip's own verbs are the members `Z80` publishes with
  // `[[=refract::operation]]`, and its locations are whatever it can `read`,
  // so neither is listed here.
  static consteval std::vector<std::meta::info> palettes() { return {^^Operations, ^^Alu}; }
};

} // namespace specbolt::v4
