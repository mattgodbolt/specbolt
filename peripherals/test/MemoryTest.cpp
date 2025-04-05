#include <catch2/catch_test_macros.hpp>
#include <memory> // For std::make_shared

#ifdef SPECBOLT_MODULES
import peripherals;
#else
#include "peripherals/Memory.hpp"
#endif


namespace specbolt {

// Mock class to track contention delays
class ContentionTracker : public Memory::Listener, public Memory::ContentionHandler {
public:
  void on_memory_read(std::uint16_t address) override {
    (void)address; // Prevent unused parameter warning
  }

  void on_memory_write(std::uint16_t address) override {
    (void)address; // Prevent unused parameter warning
  }

  std::size_t get_contention_cycles() const { return contention_cycles_; }
  void reset_contention_cycles() { contention_cycles_ = 0; }

  // Implementation of ContentionHandler interface
  std::size_t get_memory_contention_cycles(std::uint16_t address) const override {
    // In the ZX Spectrum, only addresses 0x4000-0x7FFF are contended
    // Return the contention pattern based on address and current_tstate
    if (address >= 0x4000 && address <= 0x7FFF) {
      return contention_pattern_[current_tstate_ % 8];
    }
    return 0;
  }

  // Set current T-state (0-7) to simulate ULA timing
  void set_current_tstate(std::uint8_t tstate) { current_tstate_ = tstate % 8; }

private:
  std::size_t contention_cycles_{0};
  std::uint8_t current_tstate_{0};

  // ZX Spectrum 48K ULA contention pattern (number of cycles to wait)
  // Each T-state (0-7) has a different contention value
  const std::array<std::size_t, 8> contention_pattern_{6, 5, 4, 3, 2, 1, 0, 0};
};

TEST_CASE("memory tests", "[Memory]") {
  SECTION("reads and writes") {
    Memory memory{4};
    memory.set_rom_flags({false, false, false, false});
    memory.write(0x1234, 0x55);
    CHECK(memory.read(0x1234) == 0x55);
  }

  SECTION("protects writes to ROM") {
    Memory memory{4};
    memory.raw_write(0x1234, 0x88);
    memory.set_rom_flags({true, false, false, false});
    memory.write(0x1234, 0x55);
    CHECK(memory.read(0x1234) == 0x88);
    SECTION("but not RAM") {
      memory.write(0x4000, 0x33);
      CHECK(memory.read(0x4000) == 0x33);
    }
  }

  SECTION("Spectrum 128 like behaviour") {
    Memory memory{10}; // 8 16k banks, plus two roms
    memory.set_page_table({8, 5, 2, 0});
    // Fake out the two ROMs
    for (std::uint16_t i = 0; i < 0x4000; ++i) {
      memory.raw_write(8, i, 128);
      memory.raw_write(9, i, 48);
    }
    SECTION("initial mapping") {
      CHECK(memory.read(0x0000) == 128);
      CHECK(memory.read(0x4000) == 0);
      CHECK(memory.read(0x8000) == 0);
      CHECK(memory.read(0xc000) == 0);
      memory.write(0x4000, 0xff);
      CHECK(memory.raw_read(5, 0) == 0xff);
    }
    SECTION("Check bank switching") {
      for (std::uint8_t ram_bank = 0; ram_bank < 8; ++ram_bank) {
        memory.set_page_table({8, 5, 2, ram_bank});
        memory.write(0xc000, 0xf0 | ram_bank);
      }
      for (std::uint8_t ram_bank = 0; ram_bank < 8; ++ram_bank) {
        memory.set_page_table({8, 5, 2, ram_bank});
        CHECK(memory.read(0xc000) == (0xf0 | ram_bank));
      }
      SECTION("roms") {
        memory.set_page_table({9, 5, 2, 0});
        CHECK(memory.read(0x0000) == 48);
      }
    }
  }

  SECTION("Memory contention tests") {
    Memory memory{4};
    memory.set_rom_flags({false, false, false, false});

    SECTION("No contention in ROM area") {
      auto contention_tracker = std::make_shared<ContentionTracker>();
      memory.set_contention_handler(contention_tracker);

      // Set T-state to one that would cause contention (if in contended area)
      contention_tracker->set_current_tstate(0); // Maximum contention T-state

      // Read from ROM area (0x0000-0x3FFF)
      std::uint8_t unused_value = memory.read_with_contention(0x1000); // Store it to avoid nodiscard warning
      (void)unused_value; // Suppress unused variable warning
      auto cycles = memory.get_contention_cycles(0x1000);
      CHECK(cycles == 0); // No contention in ROM
    }

    SECTION("Contention in screen RAM area") {
      auto contention_tracker = std::make_shared<ContentionTracker>();
      memory.set_contention_handler(contention_tracker);

      // Test with different T-states
      for (std::uint8_t t = 0; t < 8; t++) {
        contention_tracker->set_current_tstate(t);

        // Read from contended memory (0x4000-0x7FFF)
        std::uint8_t unused_value = memory.read_with_contention(0x4500); // Store it to avoid nodiscard warning
        (void)unused_value; // Suppress unused variable warning
        auto cycles = memory.get_contention_cycles(0x4500);

        // Expected contention based on T-state
        std::size_t expected = contention_tracker->get_memory_contention_cycles(0x4500);
        CHECK(cycles == expected);
      }
    }

    SECTION("No contention in high RAM") {
      auto contention_tracker = std::make_shared<ContentionTracker>();
      memory.set_contention_handler(contention_tracker);

      // Set T-state to one that would cause contention (if in contended area)
      contention_tracker->set_current_tstate(0);

      // Read from upper RAM (0x8000-0xFFFF)
      std::uint8_t unused_value = memory.read_with_contention(0xC000); // Store it to avoid nodiscard warning
      (void)unused_value; // Suppress unused variable warning
      auto cycles = memory.get_contention_cycles(0xC000);
      CHECK(cycles == 0); // No contention in high RAM
    }

    SECTION("Write contention") {
      auto contention_tracker = std::make_shared<ContentionTracker>();
      memory.set_contention_handler(contention_tracker);

      // Test with different T-states
      for (std::uint8_t t = 0; t < 8; t++) {
        contention_tracker->set_current_tstate(t);

        // Write to contended memory
        auto cycles = memory.write_with_contention(0x5000, 0xAA);

        // Expected contention based on T-state
        std::size_t expected = contention_tracker->get_memory_contention_cycles(0x5000);
        CHECK(cycles == expected);
      }
    }
  }
}

} // namespace specbolt
