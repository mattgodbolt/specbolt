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
