#pragma once

#include <cstdint>
#include <string>

namespace specbolt {
class Memory;
}

namespace specbolt::v2 {

struct Disassembled {
  std::string disassembly;
  std::size_t length;
};

Disassembled disassemble(const Memory &memory, std::uint16_t address);

} // namespace specbolt::v2
