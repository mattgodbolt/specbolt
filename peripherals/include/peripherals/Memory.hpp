#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <type_traits>

namespace specbolt {

class Memory {
public:
  [[nodiscard]] std::uint8_t read(std::uint16_t address) const;
  [[nodiscard]] std::uint16_t read16(std::uint16_t address) const;
  void write(uint16_t address, uint8_t byte);
  void write16(uint16_t address, uint16_t word);

  void raw_write(uint16_t address, uint8_t byte);
  void load(const std::filesystem::path &filename, uint16_t address, uint16_t size);

  void set_rom_size(const std::size_t size) { rom_size_ = size; }

  friend void write_to_memory(
      Memory &memory, std::uint16_t base_address, std::convertible_to<std::uint8_t> auto... bytes) {
    [&]<std::size_t... Idx>(std::index_sequence<Idx...>) {
      (memory.write(static_cast<std::uint16_t>(base_address + Idx), static_cast<std::uint8_t>(bytes)), ...);
    }(std::make_index_sequence<sizeof...(bytes)>());
  }

private:
  std::size_t rom_size_{0x4000};
  std::array<std::uint8_t, 65536> memory_{};
};

} // namespace specbolt
