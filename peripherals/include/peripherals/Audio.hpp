#pragma once

#ifndef SPECBOLT_MODULES
#include <cstdint>
#include <span>
#include "peripherals/Blip_Buffer.hpp"
#endif

namespace specbolt {

SPECBOLT_EXPORT class Audio {
public:
  explicit Audio(std::size_t sample_rate, std::size_t clock_rate);

  void set_output(std::size_t total_cycles, bool beeper, bool tape);
  void set_tape_input(std::size_t total_cycles, bool tape_in);

  std::vector<std::int16_t> end_frame(std::size_t total_cycles);

private:
  void update(std::size_t total_cycles);
  std::int16_t current_output_{};
  std::size_t last_frame_{};
  bool beeper_on_{};
  bool tape_output_{};
  bool tape_input_{};
  Blip_Buffer blip_buffer_;
  Blip_Synth<blip_good_quality, 65535> blip_synth_;
};

} // namespace specbolt
