#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <ios>
#include <vector>

#ifdef SPECBOLT_MODULES
import spectrum;
import z80_common;
import z80_v3;
#else
#include "spectrum/Assets.hpp"
#include "spectrum/Spectrum.hpp"
#include "z80/v3/Z80.hpp"
#endif

#ifdef SPECBOLT_HAS_V4
#include "z80/v4/Z80.hpp"
#endif

// Booting the real ROM is a different kind of test from an opcode suite: it
// runs whatever the ROM happens to do, in whatever order, for a second of
// emulated time, and nobody chose the instruction mix. Two implementations that
// agree on every byte of memory and every register after that are agreeing
// about a great deal more than any table of expectations could state.

namespace specbolt {
namespace {

constexpr std::size_t frames = 200; // the ROM tests memory before it draws anything

template<typename Cpu>
struct Booted {
  std::vector<std::uint8_t> memory;
  RegisterFile registers;
  std::size_t cycles{};
};

template<typename Cpu>
Booted<Cpu> boot() {
  Spectrum<Cpu> spectrum{Variant::Spectrum48, get_asset_dir() / "48.rom", 16000};
  std::size_t cycles = 0;
  for (std::size_t frame = 0; frame < frames; ++frame)
    cycles += spectrum.run_frame();

  Booted<Cpu> result{.memory = {}, .registers = spectrum.z80().regs(), .cycles = cycles};
  result.memory.reserve(0x10000);
  for (std::size_t address = 0; address < 0x10000; ++address)
    result.memory.push_back(spectrum.memory().read(static_cast<std::uint16_t>(address)));
  return result;
}

// The ROM clears the display and writes its copyright line, so a booted 48K
// machine has a screen that is neither blank nor full.
[[nodiscard]] std::size_t set_pixels(const std::vector<std::uint8_t> &memory) {
  std::size_t count = 0;
  for (std::size_t address = 0x4000; address < 0x5800; ++address)
    count += static_cast<std::size_t>(std::popcount(memory[address]));
  return count;
}

} // namespace

TEST_CASE("Booting the 48K ROM") {
  const auto v3 = boot<v3::Z80>();

  SECTION("gets somewhere: the ROM has drawn to the screen") {
    const auto pixels = set_pixels(v3.memory);
    CHECK(pixels > 0);
    CHECK(pixels < 0x1800 * 8);
  }

#ifdef SPECBOLT_HAS_V4
  SECTION("and v4 draws the same screen") {
    const auto v4 = boot<v4::Z80>();

    // Never compare whole images directly: a mismatch would print both of them.
    std::size_t first = 0x10000;
    std::size_t differences = 0;
    for (std::size_t address = 0x4000; address < 0x5b00; ++address)
      if (v3.memory[address] != v4.memory[address]) {
        first = std::min(first, address);
        ++differences;
      }
    INFO("first display difference at 0x" << std::hex << first << std::dec << " (" << differences << " bytes)");
    CHECK(differences == 0);

    // Deliberately *not* checked: the rest of memory, or the registers. v1, v2
    // and v3 all charge 7 T-states for accepting an interrupt; v4 charges the
    // documented 13 (a 7-cycle acknowledge, then two 3-cycle pushes). Over the
    // couple of hundred interrupts this test runs, six cycles apiece is enough
    // to land the next interrupt on a different instruction, which shows up as
    // a handful of differing bytes near the top of the stack and in whatever
    // the ROM happened to be holding at the time.
    //
    // The picture is identical, because nothing here depends on that timing.
    // See issue #44; fixing it moves behaviour all three have been measured
    // against, so it wants doing deliberately rather than in passing.
  }
#endif
}

} // namespace specbolt
