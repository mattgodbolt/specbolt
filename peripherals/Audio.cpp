#ifndef SPECBOLT_MODULES
#include "peripherals/Audio.hpp"

#include <algorithm>
#endif

namespace specbolt {

Audio::Audio(const std::size_t sample_rate, const std::size_t clock_rate) {
  blip_buffer_.clock_rate(clock_rate);
  blip_buffer_.set_sample_rate(static_cast<std::size_t>(sample_rate), 1000);
  blip_buffer_.bass_freq(200);
  blip_synth_.volume(1.0);
  blip_synth_.treble_eq(-37.0);
  blip_synth_.output(&blip_buffer_);
}

void Audio::set_output(const std::size_t total_cycles, const bool beeper_on, const bool tape_on) {
  static constexpr std::int16_t beeper_on_volume = 50 * 256;
  static constexpr std::int16_t tape_on_volume = 2 * 256;
  const auto output = static_cast<std::int16_t>((beeper_on ? beeper_on_volume : 0) + (tape_on ? tape_on_volume : 0));
  if (output == current_output_)
    return;
  blip_synth_.update(total_cycles - last_frame_, output);
  current_output_ = output;
}

std::vector<std::int16_t> Audio::end_frame(const std::size_t total_cycles) {
  blip_buffer_.end_frame(total_cycles - last_frame_);
  last_frame_ = total_cycles;
  std::vector<std::int16_t> result;
  result.resize(blip_buffer_.samples_avail());
  result.resize(blip_buffer_.read_samples(result.data(), result.size(), false));
  return result;
}

} // namespace specbolt
