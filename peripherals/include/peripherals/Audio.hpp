#pragma once

#ifndef SPECBOLT_MODULES
#include <cstdint>
#include <span>
#include "peripherals/Blip_Buffer.hpp"
#define SPECBOLT_EXPORT
#endif

namespace specbolt {

SPECBOLT_EXPORT class Audio {
public:
  explicit Audio(int sample_rate);

  void set_output(std::size_t total_cycles, bool beeper_on, bool tape_on);

  [[nodiscard]] auto sample_rate() const { return sample_rate_; }
  std::vector<std::int16_t> end_frame(std::size_t total_cycles);

  [[nodiscard]] auto overruns() const { return overruns_; }

private:
  int sample_rate_;
  std::int16_t current_output_{};
  std::size_t overruns_{};
  std::size_t last_frame_{};
  Blip_Buffer blip_buffer_;
  Blip_Synth<blip_good_quality, 65535> blip_synth_;
};

} // namespace specbolt
