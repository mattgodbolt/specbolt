#pragma once

#include <filesystem>
#include <vector>

namespace specbolt {

class Tape {
public:
  void load(const std::filesystem::path &path);

  [[nodiscard]] std::size_t next_transition() const { return next_transition_; }
  void pass_time(std::size_t num_cycles);

  [[nodiscard]] bool level() const { return level_; }

private:
  std::size_t num_edges_{};
  std::size_t next_transition_{};
  bool level_{};

  enum class State { Idle, Pilot, Sync1, Sync2, Data1, Data2, Pause };
  State state_{State::Pilot};

  std::vector<std::uint8_t> data_{};
};

} // namespace specbolt
