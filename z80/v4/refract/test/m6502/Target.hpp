#pragma once

// The 6502 as a refract target: the machine, its palette, and its description.

#include "Machine.hpp"
#include "Operations.hpp"
#include "refract/Compiled.hpp"

#include <meta>
#include <string_view>
#include <vector>

namespace specbolt::m6502 {

struct Source {
  static constexpr std::string_view file = "6502.cpu";
  static constexpr std::string_view text = [] {
    // clang-format off
    static constexpr char raw[] = {
#embed "6502.cpu"
    };
    // clang-format on
    return std::string_view{raw, sizeof raw};
  }();
};

struct Target {
  using Machine = m6502::Machine;
  using Compiled = refract::Compiled<Source>;
  static consteval std::vector<std::meta::info> palettes() { return {^^Operations}; }
};

} // namespace specbolt::m6502
