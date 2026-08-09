#pragma once

#ifndef SPECBOLT_MODULES
#include <cstdint>
#include <string>

namespace specbolt {
class Memory;
}
#endif

namespace specbolt::v4 {

SPECBOLT_EXPORT struct Disassembled {
  std::string disassembly;
  std::size_t length;
};

SPECBOLT_EXPORT Disassembled disassemble(const Memory &memory, std::uint16_t address);

} // namespace specbolt::v4
