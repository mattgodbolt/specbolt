#include "z80/v2/Z80.hpp"

#include "z80/v2/Generated.hpp"

#include <format>
#include <iostream>
#include <stdexcept>

namespace specbolt::v2 {

void Z80::execute_one() {
  if (halted_) {
    ++now_tstates_;
    return;
  }
  ++num_instructions_;
  reg_history_[current_reg_history_index_] = regs_;
  current_reg_history_index_ = (current_reg_history_index_ + 1) % RegHistory;

  regs_.r(regs_.r() + 1);
  const auto initial_pc = regs_.pc();
  try {
    decode_and_run(*this);
  }
  catch (...) {
    // TODO heinous
    regs_.pc(initial_pc);
    throw;
  }
}

void Z80::branch(const std::int8_t offset) { regs_.pc(static_cast<std::uint16_t>(regs_.pc() + offset)); }

void Z80::push16(const std::uint16_t value) {
  push8(static_cast<std::uint8_t>(value >> 8));
  push8(static_cast<std::uint8_t>(value));
}

std::uint16_t Z80::pop16() {
  const auto low = pop8();
  const auto high = pop8();
  return static_cast<std::uint16_t>(high << 8) | low;
}

void Z80::push8(const std::uint8_t value) {
  // todo maybe account for time here instead of in the instructions?
  const auto new_sp = static_cast<std::uint16_t>(regs_.sp() - 1);
  write8(new_sp, value);
  regs_.sp(new_sp);
}

std::uint8_t Z80::pop8() {
  // todo maybe account for time here instead of in the instructions?
  const auto old_sp = regs_.sp();
  regs_.sp(static_cast<std::uint16_t>(old_sp + 1));
  return read8(old_sp);
}

std::vector<RegisterFile> Z80::history() const {
  std::vector<RegisterFile> result;
  const auto num_entries = std::min(RegHistory, num_instructions_executed());
  result.reserve(num_entries + 1);
  for (auto offset = 0u; offset < num_entries; ++offset) {
    const auto index = (current_reg_history_index_ + offset) % RegHistory;
    result.push_back(reg_history_[index]);
  }
  result.push_back(regs_);
  return result;
}

void Z80::interrupt() {
  if (!iff1_)
    return;
  // Some dark business with parity flag here ignored.
  if (halted_) {
    halted_ = false;
    registers().pc(registers().pc() + 1);
  }
  iff1_ = iff2_ = false;
  pass_time(7);
  push16(registers().pc());
  switch (irq_mode_) {
    case 0:
    case 1: registers().pc(0x38); break;
    case 2: {
      // Assume the bus is at 0xff.
      const auto addr = static_cast<std::uint16_t>(0xff | (registers().i() << 8));
      registers().pc(read16(addr));
      break;
    }
    default: throw std::runtime_error("Inconceivable");
  }
}

} // namespace specbolt::v2
