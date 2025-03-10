#include "Audio.hpp"

#ifndef SPECBOLT_IN_MODULE
#include <algorithm>
#endif

namespace specbolt {

Audio::Audio(const int sample_rate) : sample_rate_(sample_rate) { audio_.resize(65536 * 4); }

void Audio::update(const std::size_t total_cycles) {
  const auto cycles_elapsed = total_cycles - cycle_count_;
  const auto total_fill = cycles_elapsed * static_cast<std::size_t>(sample_rate_) / 3'500'000ul;
  const auto to_fill = std::min(total_fill, audio_.size());
  overruns_ += total_fill - to_fill;
  for (std::size_t i = 0; i < to_fill; ++i) {
    audio_[write_pos_ % audio_.size()] = current_output_;
    ++write_pos_;
  }
  cycle_count_ = total_cycles;
}

void Audio::set_output(const std::size_t total_cycles, const bool beeper_on, const bool tape_on) {
  static constexpr std::int16_t beeper_on_volume = 50 * 256;
  static constexpr std::int16_t tape_on_volume = 2 * 256;
  const auto output = static_cast<std::int16_t>((beeper_on ? beeper_on_volume : 0) + (tape_on ? tape_on_volume : 0));
  if (output == current_output_)
    return;
  update(total_cycles);
  current_output_ = output;
}

std::span<std::int16_t> Audio::fill(const size_t total_cycles, const std::span<std::int16_t> span) {
  update(total_cycles);
  if (const auto behind = write_pos_ - read_pos_; behind > audio_.size())
    read_pos_ = write_pos_ - audio_.size();
  const auto available = std::min(write_pos_ - read_pos_, audio_.size());
  const auto to_copy = std::min(available, span.size());
  for (std::size_t i = 0; i < to_copy; ++i) {
    span[i] = audio_[read_pos_ % audio_.size()];
    read_pos_++;
  }

  auto result = span.subspan(to_copy);

  if (to_copy < span.size()) {
    std::ranges::fill(result, current_output_);
    underruns_ += span.size() - to_copy;
  }
  cycle_count_ = total_cycles;
  return result;
}

} // namespace specbolt
