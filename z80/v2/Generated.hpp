#pragma once

#include <bitset>
#include <cstdint>
#include <string>

namespace specbolt {
class Memory;
}
namespace specbolt::v2 {

class Z80;

void decode_and_run(Z80 &z80);

struct Disassembled {
  std::string disassembly;
  std::size_t length;
};

Disassembled disassemble(const Memory &memory, std::uint16_t address);

// For testing.
std::bitset<256> is_indirect_for_testing();

} // namespace specbolt::v2
