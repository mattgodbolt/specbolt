#include "spectrum/Spectrum.hpp"

namespace specbolt {

Spectrum::Spectrum(const std::filesystem::path &rom) : video_(memory_), z80_(memory_) {
  const auto file_size = std::filesystem::file_size(rom);
  constexpr std::uint16_t SpectrumRomSize = 0x4000;
  if (file_size != SpectrumRomSize)
    throw std::runtime_error("Bad size for rom file " + rom.string());
  memory_.load(rom, 0, SpectrumRomSize);
  memory_.set_rom_size(SpectrumRomSize);
}

size_t Spectrum::run_cycles(const size_t cycles) {
  size_t total_cycles_elapsed = 0;
  while (total_cycles_elapsed < cycles) {
    const auto cycles_elapsed = z80_.execute_one();
    total_cycles_elapsed += cycles_elapsed;
    video_.set_border(z80_.port_fe() & 0x7);
    video_.poll(cycles_elapsed);
  }
  return total_cycles_elapsed;
}

} // namespace specbolt
