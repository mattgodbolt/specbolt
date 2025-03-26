module;

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <vector>

export module peripherals:Memory;

namespace specbolt {

export class Memory {
public:
  explicit Memory(int num_pages);
  [[nodiscard]] std::uint8_t read(std::uint16_t address) const;
  [[nodiscard]] std::uint16_t read16(std::uint16_t address) const;
  void write(std::uint16_t address, std::uint8_t byte);
  void write16(std::uint16_t address, std::uint16_t word);

  void raw_write(std::uint16_t address, std::uint8_t byte);
  void raw_write(std::uint8_t page, std::uint16_t offset, std::uint8_t byte);
  void raw_write_checked(std::uint8_t page, std::uint16_t offset, std::uint8_t byte);
  [[nodiscard]] std::uint8_t raw_read(std::uint8_t page, std::uint16_t offset) const;

  void load(const std::filesystem::path &filename, std::uint8_t page, std::uint16_t offset, std::uint16_t size);

  void set_page_table(const std::array<std::uint8_t, 4> page_table) { page_table_ = page_table; }
  [[nodiscard]] const auto &page_table() const { return page_table_; }
  void set_rom_flags(const std::array<bool, 4> rom) { rom_ = rom; }
  [[nodiscard]] const auto &rom_flags() const { return rom_; }

  friend void write_to_memory(
      Memory &memory, std::uint16_t base_address, std::convertible_to<std::uint8_t> auto... bytes) {
    [&]<std::size_t... Idx>(std::index_sequence<Idx...>) {
      (memory.write(static_cast<std::uint16_t>(base_address + Idx), static_cast<std::uint8_t>(bytes)), ...);
    }(std::make_index_sequence<sizeof...(bytes)>());
  }

private:
  static constexpr auto page_size = 0x4000uz;
  std::array<bool, 4> rom_{true, false, false, false};
  std::array<std::uint8_t, 4> page_table_{0, 1, 2, 3};
  std::vector<std::uint8_t> address_space_{};

  [[nodiscard]] constexpr auto offset_for(const std::uint16_t address) const {
    return page_table_[address / page_size] * page_size + address % page_size;
  }
};

} // namespace specbolt

namespace specbolt {

Memory::Memory(const int num_pages) {
  if (num_pages < 4) {
    throw std::runtime_error("Memory must have at least 4 pages");
  }
  address_space_.resize(static_cast<std::size_t>(num_pages) * page_size);
}

std::uint8_t Memory::read(const std::uint16_t address) const { return address_space_[offset_for(address)]; }

std::uint16_t Memory::read16(const std::uint16_t address) const {
  return static_cast<std::uint16_t>(read(address + 1) << 8 | read(address));
}

void Memory::write(const std::uint16_t address, const std::uint8_t byte) {
  if (rom_[address / page_size])
    return;
  raw_write(address, byte);
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
