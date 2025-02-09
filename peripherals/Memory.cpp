#include "Memory.hpp"

#include <cstring>
#include <fstream>

namespace specbolt {

std::uint8_t Memory::read(std::uint16_t address) const { return memory_[address]; }

void Memory::write(uint16_t address, uint8_t byte) {
  // protect ROM
  if (address >= 16384)
    raw_write(address, byte);
}
void Memory::raw_write(uint16_t address, uint8_t byte) { memory_[address] = byte; }
void Memory::load(const std::filesystem::path &filename, const uint16_t address, const uint16_t size) {
  std::ifstream load_stream(filename, std::ios::binary);

  if (!load_stream) {
    throw std::runtime_error(std::format("Failed to open file '{}': {}", filename.c_str(), std::strerror(errno)));
  }

  if ((static_cast<size_t>(address) + size) > memory_.size()) {
    throw std::runtime_error(std::format(
        "Trying to load outside of available memory {} + {} > {}", address, size, memory_.size()));
  }

  load_stream.read(reinterpret_cast<char *>(memory_.data() + address), size);

  if (!load_stream) {
    throw std::runtime_error(
        std::format("Unable to read file '{}' (read size = {} bytes)", filename.c_str(), size));
  }
}

} // namespace specbolt
