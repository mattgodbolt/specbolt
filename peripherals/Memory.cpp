#ifndef SPECBOLT_MODULES
#include "peripherals/Memory.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#endif

// These functions are declared in global scope
extern "C" {
void notify_memory_read(std::uint16_t addr);
void notify_memory_write(std::uint16_t addr);
}

namespace specbolt {

Memory::Memory(const int num_pages) {
  if (num_pages < 4) {
    throw std::runtime_error("Memory must have at least 4 pages");
  }
  address_space_.resize(static_cast<std::size_t>(num_pages) * page_size);
}

std::uint8_t Memory::read(const std::uint16_t addr) const {
  // Notify the memory tracer about this read
  notify_memory_read(addr);
  return address_space_[offset_for(addr)];
}

std::uint16_t Memory::read16(const std::uint16_t address) const {
  return static_cast<std::uint16_t>(read(address + 1) << 8 | read(address));
}

void Memory::write(const std::uint16_t addr, const std::uint8_t byte) {
  // Notify the memory tracer about this write attempt
  notify_memory_write(addr);

  if (rom_[addr / page_size])
    return;
  raw_write(addr, byte);
}

void Memory::write16(const std::uint16_t address, const std::uint16_t word) {
  write(address, static_cast<std::uint8_t>(word));
  write(address + 1, static_cast<std::uint8_t>(word >> 8));
}

void Memory::raw_write(const std::uint16_t address, const std::uint8_t byte) {
  address_space_[offset_for(address)] = byte;
}

void Memory::raw_write(const std::uint8_t page, const std::uint16_t offset, const std::uint8_t byte) {
  address_space_[page * page_size + offset] = byte;
}

void Memory::raw_write_checked(const std::uint8_t page, const std::uint16_t offset, const std::uint8_t byte) {
  address_space_.at(page * page_size + offset) = byte;
}

std::uint8_t Memory::raw_read(const std::uint8_t page, const std::uint16_t offset) const {
  return address_space_[page * page_size + offset];
}

void Memory::load(const std::filesystem::path &filename, const std::uint8_t page, const std::uint16_t offset,
    const std::uint16_t size) {
  std::ifstream load_stream(filename, std::ios::binary);

  if (!load_stream) {
    throw std::runtime_error(std::format("Failed to open file '{}': {}", filename.c_str(), std::strerror(errno)));
  }

  const auto raw_offset = page * page_size + offset;

  if ((raw_offset + size) > address_space_.size()) {
    throw std::runtime_error(std::format(
        "Trying to load outside of available memory {} + {} > {}", raw_offset, size, address_space_.size()));
  }

  load_stream.read(reinterpret_cast<char *>(address_space_.data() + raw_offset), size);

  if (!load_stream) {
    throw std::runtime_error(std::format("Unable to read file '{}' (read size = {} bytes)", filename.c_str(), size));
  }
}

} // namespace specbolt
