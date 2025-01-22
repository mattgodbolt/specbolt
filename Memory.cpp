#include "Memory.hpp"

namespace specbolt {

std::uint8_t Memory::read(std::uint16_t address) const { return memory_[address]; }

void Memory::write(uint16_t address, uint8_t byte) {
  if (address >= 16384)
    memory_[address] = byte;
}
void Memory::raw_write(uint16_t address, uint8_t byte) { memory_[address] = byte; }

} // namespace specbolt
