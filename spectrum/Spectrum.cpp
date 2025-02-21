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
  });
  z80_.add_in_handler([this](const std::uint16_t port) { return keyboard_.in(port); });
  // Handler for ULA sound...
  z80_.add_in_handler([](const std::uint16_t port) {
    // TODO something with the EAR bit here...
    return port & 1 ? std::nullopt : std::make_optional<std::uint8_t>(~(1 << 6));
  });
}

size_t Spectrum::run_cycles(const size_t cycles) {
  size_t total_cycles_elapsed = 0;
  while (total_cycles_elapsed < cycles) {
    if (trace_next_instructions_ && !z80_.halted()) {
      static size_t last_instr = 0;
      static constexpr auto UndocMask = static_cast<std::uint16_t>(0xff00 | ~(Flags::Flag3() | Flags::Flag5()).to_u8());
      const auto time_taken = z80_.cycle_count() - last_instr;
      last_instr = z80_.cycle_count();
      std::print(std::cout, "{:02} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x}\n", time_taken, z80_.pc(),
          z80_.registers().get(RegisterFile::R16::AF) & UndocMask, z80_.registers().get(RegisterFile::R16::BC),
          z80_.registers().get(RegisterFile::R16::DE), z80_.registers().get(RegisterFile::R16::HL),
          z80_.registers().ix(), z80_.registers().iy(), z80_.registers().sp());
      --trace_next_instructions_;
    }
    const auto cycles_elapsed = z80_.execute_one();
    total_cycles_elapsed += cycles_elapsed;
    if (video_.poll(cycles_elapsed))
      z80_.interrupt();
  }
  return total_cycles_elapsed;
}

} // namespace specbolt
