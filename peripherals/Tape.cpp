#include "peripherals/Tape.hpp"


namespace specbolt {

void Tape::load(const std::filesystem::path &) {}

void Tape::pass_time(const std::size_t num_cycles) {
  next_transition_ -= std::max(num_cycles, next_transition_);
  if (next_transition_ <= 0) {
    next_transition_ = 2168;
    level_ = !level_;
  }
}

} // namespace specbolt
