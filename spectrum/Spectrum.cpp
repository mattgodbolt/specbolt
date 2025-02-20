#include "spectrum/Spectrum.hpp"

#include <iostream>

namespace specbolt {

Spectrum::Spectrum(const std::filesystem::path &rom, int audio_sample_rate) :
    video_(memory_), audio_(audio_sample_rate), z80_(memory_) {
  const auto file_size = std::filesystem::file_size(rom);
  constexpr std::uint16_t SpectrumRomSize = 0x4000;
  if (file_size != SpectrumRomSize)
    throw std::runtime_error("Bad size for rom file " + rom.string());
  memory_.load(rom, 0, SpectrumRomSize);
  memory_.set_rom_size(SpectrumRomSize);
  z80_.add_out_handler([this](const std::uint16_t port, const std::uint8_t value) {
    if ((port & 0xff) == 0xfe) {
      video_.set_border(value & 0x07);
      audio_.set_output(z80_.cycle_count(), value & 0x18);
    }
    else
      std::print(std::cout, "Unexpected OUT({:04x}, {:02x})\n", port, value);
  });
  z80_.add_in_handler([this](const std::uint16_t port) { return keyboard_.in(port); });
}

size_t Spectrum::run_cycles(const size_t cycles) {
  size_t total_cycles_elapsed = 0;
  while (total_cycles_elapsed < cycles) {
    const auto cycles_elapsed = z80_.execute_one();
    total_cycles_elapsed += cycles_elapsed;
    if (video_.poll(cycles_elapsed))
      z80_.interrupt();
  }
  return total_cycles_elapsed;
}

} // namespace specbolt
