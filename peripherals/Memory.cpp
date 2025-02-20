#include "Memory.hpp"

#include <cstring>
#include <fstream>

namespace specbolt {

std::uint8_t Memory::read(const std::uint16_t address) const { return memory_[address]; }
std::uint16_t Memory::read16(const std::uint16_t address) const {
  return static_cast<uint16_t>(read(address + 1) << 8 | read(address));
}

void Memory::write(const uint16_t address, const uint8_t byte) {
  // protect ROM
  if (address >= rom_size_)
    raw_write(address, byte);
}

void Memory::write16(const uint16_t address, const uint16_t word) {
  write(address, static_cast<uint8_t>(word));
  write(address + 1, static_cast<uint8_t>(word >> 8));
}

void Memory::raw_write(const uint16_t address, const uint8_t byte) { memory_[address] = byte; }

void Memory::load(const std::filesystem::path &filename, const uint16_t address, const uint16_t size) {
  std::ifstream load_stream(filename, std::ios::binary);

  if (!load_stream) {
    throw std::runtime_error(std::format("Failed to open file '{}': {}", filename.c_str(), std::strerror(errno)));
  }

  if ((static_cast<size_t>(address) + size) > memory_.size()) {
    throw std::runtime_error(
        std::format("Trying to load outside of available memory {} + {} > {}", address, size, memory_.size()));
  }

  load_stream.read(reinterpret_cast<char *>(memory_.data() + address), size);

  if (!load_stream) {
    throw std::runtime_error(std::format("Unable to read file '{}' (read size = {} bytes)", filename.c_str(), size));
  }
}

} // namespace specbolt
