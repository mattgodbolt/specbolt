#pragma once

#include "peripherals/Audio.hpp"
#include "peripherals/Keyboard.hpp"
#include "peripherals/Memory.hpp"
#include "peripherals/Video.hpp"
#include "z80/Z80.hpp"

#include <filesystem>

namespace specbolt {

class Spectrum {
public:
  explicit Spectrum(const std::filesystem::path &rom, int audio_sample_rate);

  static constexpr auto cycles_per_frame = static_cast<std::size_t>(3.5 * 1'000'000 / 50);

  size_t run_cycles(size_t cycles);
  size_t run_frame() { return run_cycles(cycles_per_frame); }

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
  Z80 z80_;
  std::size_t trace_next_instructions_{};
};

} // namespace specbolt
