module;

#include <cstdint>
#include <vector>

export module peripherals:Audio;

import :Blip_Buffer;

namespace specbolt {
export class Audio {
public:
  explicit Audio(const std::size_t sample_rate, const std::size_t clock_rate) {
    blip_buffer_.clock_rate(clock_rate);
    blip_buffer_.set_sample_rate(sample_rate, 1000);
    blip_buffer_.bass_freq(200);
    blip_synth_.volume(1.0);
    blip_synth_.treble_eq(-37.0);
    blip_synth_.output(&blip_buffer_);
  }


  void set_output(const std::size_t total_cycles, const bool beeper, const bool tape) {
    beeper_on_ = beeper;
    tape_output_ = tape;
    update(total_cycles);
  }
  void set_tape_input(const std::size_t total_cycles, const bool tape_in) {
    tape_input_ = tape_in;
    update(total_cycles);
  }

  std::vector<std::int16_t> end_frame(const std::size_t total_cycles) {
    blip_buffer_.end_frame(total_cycles - last_frame_);
    last_frame_ = total_cycles;
    std::vector<std::int16_t> result;
    result.resize(blip_buffer_.samples_avail());
    result.resize(blip_buffer_.read_samples(result.data(), result.size(), false));
    return result;
  }

private:
  void update(const std::size_t total_cycles) {
    static constexpr std::int16_t beeper_on_volume = 50 * 256;
    static constexpr std::int16_t tape_on_volume = 5 * 256;
    const auto output = static_cast<std::int16_t>(
        (beeper_on_ ? beeper_on_volume : 0) + (tape_output_ ^ tape_input_ ? tape_on_volume : 0));
    if (output == current_output_)
      return;
    blip_synth_.update(total_cycles - last_frame_, output);
    current_output_ = output;
  }
  std::int16_t current_output_{};
  std::size_t last_frame_{};
  bool beeper_on_{};
  bool tape_output_{};
  bool tape_input_{};
  Blip_Buffer blip_buffer_;
  Blip_Synth<blip_good_quality, 65535> blip_synth_;
};

} // namespace specbolt
