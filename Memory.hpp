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

private:
  std::array<std::uint8_t, 65536> memory_{};
};

} // namespace specbolt
