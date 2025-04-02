#pragma once

#ifndef SPECBOLT_MODULES
#include <filesystem>
#include <vector>
#endif

namespace specbolt {

SPECBOLT_EXPORT
class Tape {
public:
  void load(const std::filesystem::path &path);

  [[nodiscard]] std::size_t next_transition() const { return next_transition_; }
  void pass_time(std::size_t num_cycles);

  [[nodiscard]] bool level() const { return level_; }

  void play();
  void stop();
  [[nodiscard]] bool playing() const { return state_ != State::Idle; }

private:
  std::size_t num_edges_{};
  std::size_t next_transition_{};
  std::size_t bit_cycles_{};
  bool level_{};

  enum class State { Idle, Pilot, Sync1, Sync2, Data1, Data2, Pause };
  State state_{State::Idle};

  std::size_t current_block_index_{};
  std::size_t bit_offset_{};
  struct Block {
    std::vector<std::uint8_t> data;
    std::size_t pause_cycles{};
    std::size_t pilot_edge_count{};
  };
  std::vector<Block> blocks_{};
  [[nodiscard]] const Block *current_block() const {
    return current_block_index_ < blocks_.size() ? &blocks_[current_block_index_] : nullptr;
  }
  void next();
  State next_bit();
};

} // namespace specbolt
