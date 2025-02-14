#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

namespace specbolt {

class Memory {
public:
  [[nodiscard]] std::uint8_t read(std::uint16_t address) const;
  void write(uint16_t address, uint8_t byte);

  void raw_write(uint16_t address, uint8_t byte);
  void load(const std::filesystem::path &filename, uint16_t address, uint16_t size);

  void set_rom_size(const std::size_t size) { rom_size_ = size; }

private:
  std::size_t rom_size_{0x4000};
  std::array<std::uint8_t, 65536> memory_{};
};

} // namespace specbolt
