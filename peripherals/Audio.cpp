#ifndef SPECBOLT_MODULES
#include "peripherals/Audio.hpp"
#endif

namespace specbolt {

Audio::Audio(const std::size_t sample_rate, const std::size_t clock_rate) {
  blip_buffer_.clock_rate(clock_rate);
  blip_buffer_.set_sample_rate(sample_rate, 1000);
  blip_buffer_.bass_freq(200);
  blip_synth_.volume(1.0);
  blip_synth_.treble_eq(-37.0);
  blip_synth_.output(&blip_buffer_);
}

void Audio::set_output(const std::size_t total_cycles, const bool beeper, const bool tape) {
  beeper_on_ = beeper;
  tape_output_ = tape;
  update(total_cycles);
}

void Audio::set_tape_input(const std::size_t total_cycles, const bool tape_in) {
  tape_input_ = tape_in;
  update(total_cycles);
}

void Audio::update(const std::size_t total_cycles) {
  static constexpr std::int16_t beeper_on_volume = 50 * 256;
  static constexpr std::int16_t tape_on_volume = 5 * 256;
  const auto output = static_cast<std::int16_t>(
      (beeper_on_ ? beeper_on_volume : 0) + (tape_output_ ^ tape_input_ ? tape_on_volume : 0));
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
