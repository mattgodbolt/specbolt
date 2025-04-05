#pragma once

#ifndef SPECBOLT_MODULES
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>
#endif

namespace specbolt {

SPECBOLT_EXPORT class Memory {
public:
  // Interface for memory access listeners
  class Listener {
  public:
    virtual ~Listener() = default;

    // Called when memory is read
    virtual void on_memory_read(std::uint16_t address) = 0;

    // Called when memory is written
    virtual void on_memory_write(std::uint16_t address) = 0;
  };

  // Interface for memory contention handling
  class ContentionHandler {
  public:
    virtual ~ContentionHandler() = default;

    // Returns the number of contention cycles for a given memory address
    // based on current ULA timing state
    virtual std::size_t get_memory_contention_cycles(std::uint16_t address) const = 0;
  };

  explicit Memory(int num_pages);
  [[nodiscard]] std::uint8_t read(std::uint16_t address) const;
  [[nodiscard]] std::uint16_t read16(std::uint16_t address) const;
  void write(std::uint16_t address, std::uint8_t byte);
  void write16(std::uint16_t address, std::uint16_t word);

  // Methods that include memory contention - they apply contention and return the value
  [[nodiscard]] std::uint8_t read_with_contention(std::uint16_t address) const;
  [[nodiscard]] std::uint16_t read16_with_contention(std::uint16_t address) const;
  // These methods apply contention and return the number of contention cycles
  [[nodiscard]] std::size_t write_with_contention(std::uint16_t address, std::uint8_t byte);
  [[nodiscard]] std::size_t write16_with_contention(std::uint16_t address, std::uint16_t word);

  void raw_write(std::uint16_t address, std::uint8_t byte);
  void raw_write(std::uint8_t page, std::uint16_t offset, std::uint8_t byte);
  void raw_write_checked(std::uint8_t page, std::uint16_t offset, std::uint8_t byte);
  [[nodiscard]] std::uint8_t raw_read(std::uint8_t page, std::uint16_t offset) const;

  void load(const std::filesystem::path &filename, std::uint8_t page, std::uint16_t offset, std::uint16_t size);

  void set_page_table(const std::array<std::uint8_t, 4> page_table) { page_table_ = page_table; }
  [[nodiscard]] const auto &page_table() const { return page_table_; }
  void set_rom_flags(const std::array<bool, 4> rom) { rom_ = rom; }
  [[nodiscard]] const auto &rom_flags() const { return rom_; }

  // Set a memory access listener (or nullptr to disable)
  // Note: Memory does not own the listener - caller must ensure the listener outlives the Memory
  void set_listener(Listener *listener) { listener_ = listener; }
  [[nodiscard]] bool has_listener() const { return listener_ != nullptr; }

  // Set a memory contention handler (or nullptr to disable)
  void set_contention_handler(std::shared_ptr<ContentionHandler> handler) { contention_handler_ = handler; }
  [[nodiscard]] bool has_contention_handler() const { return contention_handler_ != nullptr; }

  // Get contention cycles for a specific address (0 if no handler)
  [[nodiscard]] std::size_t get_contention_cycles(std::uint16_t address) const;

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
  Listener *listener_{nullptr}; // Optional memory access listener (not owned)
  std::shared_ptr<ContentionHandler> contention_handler_{nullptr}; // Handler for memory contention

  [[nodiscard]] constexpr auto offset_for(const std::uint16_t address) const {
    return page_table_[address / page_size] * page_size + address % page_size;
  }
};

} // namespace specbolt
