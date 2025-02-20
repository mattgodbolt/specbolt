#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace specbolt {

class Audio {
public:
  explicit Audio(int sample_rate);

  void update(std::size_t total_cycles);
  void set_output(std::size_t total_cycles, bool on);

  auto sample_rate() const { return sample_rate_; }
  void fill(size_t total_cycles, std::span<std::int16_t> span);

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
