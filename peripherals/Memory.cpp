#ifndef SPECBOLT_MODULES
#include "peripherals/Memory.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#endif

namespace specbolt {

Memory::Memory(const int num_pages) {
  if (num_pages < 4) {
    throw std::runtime_error("Memory must have at least 4 pages");
  }
  address_space_.resize(static_cast<std::size_t>(num_pages) * page_size);
}

std::uint8_t Memory::read(const std::uint16_t address) const {
  // Notify the memory listener about this read if one is registered
  if (listener_) {
    listener_->on_memory_read(address);
  }
  return address_space_[offset_for(address)];
}

std::uint16_t Memory::read16(const std::uint16_t address) const {
  return static_cast<std::uint16_t>(read(address + 1) << 8 | read(address));
}

void Memory::write(const std::uint16_t address, const std::uint8_t byte) {
  // Notify the memory listener about this write if one is registered
  if (listener_) {
    listener_->on_memory_write(address);
  }

  if (rom_[address / page_size])
    return;
  raw_write(address, byte);
}

void Memory::write16(const std::uint16_t address, const std::uint16_t word) {
  write(address, static_cast<std::uint8_t>(word));
  write(address + 1, static_cast<std::uint8_t>(word >> 8));
}

std::size_t Memory::get_contention_cycles(std::uint16_t address) const {
  if (!contention_handler_) {
    return 0;
  }
  return contention_handler_->get_memory_contention_cycles(address);
}

std::uint8_t Memory::read_with_contention(const std::uint16_t address) const {
  // Calculate memory contention cycles for this address
  // In ZX Spectrum, only addresses 0x4000-0x7FFF are contended
  // Just call get_contention_cycles but don't store the result
  // The CPU will query this directly when needed
  (void)get_contention_cycles(address);

  // Standard memory read
  return read(address);
}

std::uint16_t Memory::read16_with_contention(const std::uint16_t address) const {
  // For a 16-bit read, we need to count contention for both bytes
  // Just call get_contention_cycles but don't store the result
  (void)get_contention_cycles(address);
  (void)get_contention_cycles(address + 1);

  // Standard 16-bit read
  return static_cast<std::uint16_t>(read(address + 1) << 8 | read(address));
}

std::size_t Memory::write_with_contention(const std::uint16_t address, const std::uint8_t byte) {
  // Calculate memory contention cycles for this address
  const auto contention_cycles = get_contention_cycles(address);

  // Standard memory write
  write(address, byte);

  return contention_cycles;
}

std::size_t Memory::write16_with_contention(const std::uint16_t address, const std::uint16_t word) {
  // For a 16-bit write, we need to count contention for both bytes
  const auto low_contention = get_contention_cycles(address);
  const auto high_contention = get_contention_cycles(address + 1);

  // Standard 16-bit write
  write(address, static_cast<std::uint8_t>(word));
  write(address + 1, static_cast<std::uint8_t>(word >> 8));

  return low_contention + high_contention;
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
