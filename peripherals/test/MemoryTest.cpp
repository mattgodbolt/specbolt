#ifdef SPECBOLT_MODULES
import peripherals;
#else
#include "peripherals/Memory.hpp"
#endif

#include <catch2/catch_test_macros.hpp>

namespace specbolt {

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
}

} // namespace specbolt
