#ifndef SPECBOLT_MODULES
#include "z80/common/Flags.hpp"

#include <format>
#endif

namespace specbolt {

std::string Flags::to_string() const {
  return std::format("{}{}{}{}{}{}{}{} (0x{:02x})", //
      sign() ? "S" : "s", //
      zero() ? "Z" : "z", //
      value_ & Flag::flag5 ? "5" : "_", //
      half_carry() ? "H" : "h", //
      value_ & Flag::flag3 ? "3" : "_", //
      parity() ? "P" : "p", //
      subtract() ? "N" : "n", //
      carry() ? "C" : "c", //
      value_);
}

} // namespace specbolt
