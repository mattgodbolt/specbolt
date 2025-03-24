#ifndef SPECBOLT_MODULES
#include "peripherals/Tape.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#endif

namespace specbolt {

using namespace std::literals;

namespace {

constexpr auto PilotCycles = 2168zu;
constexpr auto Sync1Cycles = 667zu;
constexpr auto Sync2Cycles = 735zu;
constexpr auto Data0Cycles = 855zu;
constexpr auto Data1Cycles = 1710zu;
constexpr auto PilotDataEdges = 3223zu;
constexpr auto PilotHeaderEdges = 8063zu;

std::vector<std::uint8_t> read_block(std::ifstream &input) {
  std::uint16_t length{};
  input.read(reinterpret_cast<char *>(&length), sizeof(length));
  if (input.eof())
    return {};
  if (!input)
    throw std::runtime_error("Failed to read length");
  std::vector<std::uint8_t> output;
  output.resize(length);
  input.read(reinterpret_cast<char *>(output.data()), static_cast<std::streamsize>(output.size()));
  if (!input)
    throw std::runtime_error("Failed to read data");
  return output;
}

template<typename Duration>
std::size_t cycles_for(const Duration duration) {
  return (static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()) *
             3'500'000zu) /
         1'000zu;
}

} // namespace

void Tape::load(const std::filesystem::path &path) {
  std::ifstream load_stream(path, std::ios::binary);
  if (!load_stream) {
    throw std::runtime_error(std::format("Failed to open file '{}': {}", path.string(), std::strerror(errno)));
  }
  while (!load_stream.eof()) {
    if (auto block = read_block(load_stream); !block.empty()) {
      const auto header_byte = block[0];
      blocks_.push_back(
          Block{std::move(block), cycles_for(1s), header_byte & 0x80 ? PilotDataEdges : PilotHeaderEdges});
    }
  }
}

void Tape::pass_time(const std::size_t num_cycles) {
  next_transition_ -= std::min(num_cycles, next_transition_);
  if (next_transition_ == 0)
    next();
}

void Tape::play() {
  const auto *block = current_block();
  if (!block || state_ != State::Idle)
    return;
  state_ = State::Pilot;
  next_transition_ = 1;
  num_edges_ = block->pilot_edge_count;
}

void Tape::next() {
  level_ = !level_;
  switch (state_) {
    case State::Pilot:
      next_transition_ = PilotCycles;
      if (--num_edges_ == 0)
        state_ = State::Sync1;
      break;
    case State::Sync1:
      next_transition_ = Sync1Cycles;
      state_ = State::Sync2;
      break;
    case State::Sync2:
      next_transition_ = Sync2Cycles;
      state_ = next_bit();
      break;
    case State::Data1:
      next_transition_ = bit_cycles_;
      state_ = State::Data2;
      break;
    case State::Data2:
      next_transition_ = bit_cycles_;
      state_ = next_bit();
      break;
    case State::Pause:
      next_transition_ = bit_cycles_;
      if (const auto *block = current_block(); block) {
        num_edges_ = block->pilot_edge_count;
        state_ = State::Pilot;
      }
      break;
    case State::Idle: next_transition_ = 0; break;
  }
}

Tape::State Tape::next_bit() {
  const auto *block = current_block();
  if (!block)
    return State::Idle;
  const auto byte_offset = bit_offset_ / 8;
  if (byte_offset >= block->data.size()) {
    bit_cycles_ = block->pause_cycles;
    bit_offset_ = 0;
    current_block_index_++;
    return State::Pause;
  }
  bit_cycles_ = block->data[byte_offset] & (1 << (7 - (bit_offset_ % 8))) ? Data1Cycles : Data0Cycles;
  ++bit_offset_;
  return State::Data1;
}

} // namespace specbolt
