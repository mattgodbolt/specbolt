#pragma once

#ifndef SPECBOLT_MODULES
#include <cstdint>
#include <span>
#include <vector>
#define SPECBOLT_EXPORT
#endif

namespace specbolt {

SPECBOLT_EXPORT class Audio {
public:
  explicit Audio(int sample_rate);

  void update(std::size_t total_cycles);
  void set_output(std::size_t total_cycles, bool beeper_on, bool tape_on);

  [[nodiscard]] auto sample_rate() const { return sample_rate_; }
  std::span<std::int16_t> fill(std::size_t total_cycles, std::span<std::int16_t> span);

  [[nodiscard]] auto underruns() const { return underruns_; }
  [[nodiscard]] auto overruns() const { return overruns_; }

private:
  int sample_rate_;
  std::size_t cycle_count_{};
  std::int16_t current_output_{};
  std::size_t read_pos_{};
  std::size_t write_pos_{};
  std::vector<std::int16_t> audio_{};
  std::size_t overruns_{};
  std::size_t underruns_{};
};

} // namespace specbolt
