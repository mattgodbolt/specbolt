#pragma once

#ifndef SPECBOLT_IN_MODULE
#include <bitset>
#include <cstdint>
#include <string>
#endif

#include "module.hpp"

namespace specbolt {

#ifndef SPECBOLT_IN_MODULE
class Memory;
class Z80;
#endif

SPECBOLT_EXPORT using Z80_Op = void(Z80 &);

SPECBOLT_EXPORT void decode_and_run(Z80 &z80);

SPECBOLT_EXPORT struct Disassembled {
  std::string disassembly;
  std::size_t length;
};

SPECBOLT_EXPORT Disassembled disassemble(const Memory &memory, std::uint16_t address);

// For testing.
SPECBOLT_EXPORT std::bitset<256> is_indirect_for_testing();

} // namespace specbolt
