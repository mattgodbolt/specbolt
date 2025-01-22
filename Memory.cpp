#include "Memory.hpp"

#include <cstring>
#include <fstream>

namespace specbolt {

std::uint8_t Memory::read(std::uint16_t address) const { return memory_[address]; }

void Memory::write(uint16_t address, uint8_t byte) {
  if (address >= 16384)
    memory_[address] = byte;
}
void Memory::raw_write(uint16_t address, uint8_t byte) { memory_[address] = byte; }
void Memory::load(const std::filesystem::path &filename, const uint16_t address, const uint16_t size) {
  std::ifstream load_stream(filename);
  if (!load_stream) {
    throw std::runtime_error(std::format("Failed to open file '{}': {}", filename.c_str(), std::strerror(errno)));
  }
  load_stream.read(reinterpret_cast<char *>(memory_.data() + address), size);
  // TODO error handling?
}

} // namespace specbolt
