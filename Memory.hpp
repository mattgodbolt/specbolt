#pragma once

#include <array>
#include <cstdint>

namespace specbolt {

class Memory {
public:
  std::uint8_t read(std::uint16_t address) const;
  void write(uint16_t address, uint8_t byte);

  void raw_write(uint16_t address, uint8_t byte);

private:
  std::array<std::uint8_t, 65536> memory_{};
};

} // namespace specbolt
