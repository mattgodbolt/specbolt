#pragma once

#include <cstdint>
#include <string>

namespace specbolt {
class Memory;
}

namespace specbolt::v4 {

SPECBOLT_EXPORT struct Disassembled {
  std::string disassembly;
  std::size_t length;
};

SPECBOLT_EXPORT [[nodiscard]] Disassembled disassemble(const Memory &memory, std::uint16_t address);

} // namespace specbolt::v4
