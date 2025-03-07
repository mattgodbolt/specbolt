#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace specbolt {

class Memory;
class Z80;

using Z80_Op = void(Z80 &);

void decode_and_run(Z80 &z80);

struct Disassembled {
  std::string disassembly;
  std::size_t length;
};

Disassembled disassemble(const Memory &memory, std::uint16_t address);

} // namespace specbolt
