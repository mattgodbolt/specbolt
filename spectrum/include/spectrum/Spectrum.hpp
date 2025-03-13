#pragma once

#ifndef SPECBOLT_MODULES
#include "peripherals/Audio.hpp"
#include "peripherals/Keyboard.hpp"
#include "peripherals/Memory.hpp"
#include "peripherals/Video.hpp"

#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"

#include <filesystem>
#include <iostream>

#endif

namespace specbolt {

SPECBOLT_EXPORT
template<typename Z80Impl>
class Spectrum {
public:
  explicit Spectrum(const std::filesystem::path &rom, const int audio_sample_rate) :
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
        audio_.set_output(z80_.cycle_count(), value & 0x10, value & 0x8);
      }
    });
    z80_.add_in_handler([this](const std::uint16_t port) { return keyboard_.in(port); });
    // Handler for ULA sound...
    z80_.add_in_handler([](const std::uint16_t port) {
      // TODO something with the EAR bit here...
      return port & 1 ? std::nullopt : std::make_optional<std::uint8_t>(~(1 << 6));
    });
  }
  static constexpr auto cycles_per_frame = static_cast<std::size_t>(3.5 * 1'000'000 / 50);

  std::size_t run_cycles(const std::size_t cycles) {
    // TODO fix up this mess of cycles elapsed etc
    std::size_t total_cycles_elapsed = 0;
    while (total_cycles_elapsed < cycles) {
      if (trace_next_instructions_ && !z80_.halted()) {
        static constexpr auto UndocMask =
            static_cast<std::uint16_t>(0xff00 | ~(Flags::Flag3() | Flags::Flag5()).to_u8());
        const auto time_taken = z80_.cycle_count() - last_traced_instr_cycle_count_;
        last_traced_instr_cycle_count_ = z80_.cycle_count();
        std::print(std::cout, "{:02} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x}\n", time_taken, z80_.pc(),
            z80_.registers().get(RegisterFile::R16::AF) & UndocMask, z80_.registers().get(RegisterFile::R16::BC),
            z80_.registers().get(RegisterFile::R16::DE), z80_.registers().get(RegisterFile::R16::HL),
            z80_.registers().ix(), z80_.registers().iy(), z80_.registers().sp());
        --trace_next_instructions_;
      }
      const auto before = z80_.cycle_count();
      z80_.execute_one();
      const auto cycles_elapsed = z80_.cycle_count() - before;
      total_cycles_elapsed += cycles_elapsed;
      if (video_.poll(cycles_elapsed))
        z80_.interrupt();
    }
    return total_cycles_elapsed;
  }

  std::size_t run_frame() { return run_cycles(cycles_per_frame); }

  [[nodiscard]] auto &z80() { return z80_; }
  [[nodiscard]] const auto &z80() const { return z80_; }
  [[nodiscard]] auto &video() { return video_; }
  [[nodiscard]] auto &memory() { return memory_; }
  [[nodiscard]] auto &keyboard() { return keyboard_; }
  [[nodiscard]] auto &audio() { return audio_; }

  void trace_next(const std::size_t instructions) { trace_next_instructions_ = instructions; }

private:
  Memory memory_;
  Video video_;
  Audio audio_;
  Keyboard keyboard_;
  Z80Impl z80_;
  std::size_t trace_next_instructions_{};
  std::size_t last_traced_instr_cycle_count_{};
};

} // namespace specbolt
