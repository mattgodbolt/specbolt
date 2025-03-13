#ifndef SPECBOLT_MODULES
#include "z80/v2/Z80.hpp"
#include "z80/v2/Z80Impl.hpp"

#include <iostream>
#include <stdexcept>
#endif

namespace specbolt::v2 {

void Z80::execute_one() {
  if (halted_) {
    ++now_tstates_;
    return;
  }
  ++num_instructions_;
  reg_history_[current_reg_history_index_] = regs_;
  current_reg_history_index_ = (current_reg_history_index_ + 1) % RegHistory;

  const auto opcode = read_opcode();
  impl::table<impl::build_execute_hl>[opcode](*this);
}

void Z80::branch(const std::int8_t offset) { regs_.pc(static_cast<std::uint16_t>(regs_.pc() + offset)); }

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

std::uint8_t Z80::read_opcode() {
  const auto opcode = read_immediate();
  // One cycle refresh.
  regs_.r((regs_.r() & 0x80) | ((regs_.r() + 1) & 0x7f));
  pass_time(1);
  return opcode;
}


std::uint8_t Z80::read_immediate() {
  pass_time(3);
  const auto addr = regs_.pc();
  regs_.pc(addr + 1);
  return memory_.read(addr);
}

std::uint16_t Z80::read_immediate16() {
  const auto low = read_immediate();
  const auto high = read_immediate();
  return static_cast<std::uint16_t>(high << 8 | low);
}
void Z80::write(const std::uint16_t address, const std::uint8_t byte) {
  pass_time(3);
  memory_.write(address, byte);
}

std::uint8_t Z80::read(const std::uint16_t address) {
  pass_time(3);
  return memory_.read(address);
}

std::uint8_t Z80::pop8() {
  const auto value = read(regs_.sp());
  regs_.sp(regs_.sp() + 1);
  return value;
}

std::uint16_t Z80::pop16() {
  const auto low = pop8();
  const auto high = pop8();
  return static_cast<std::uint16_t>(high << 8 | low);
}

void Z80::push8(const std::uint8_t value) {
  regs_.sp(regs_.sp() - 1);
  write(regs_.sp(), value);
}

void Z80::push16(const std::uint16_t value) {
  push8(static_cast<std::uint8_t>(value >> 8));
  push8(static_cast<std::uint8_t>(value));
}

void Z80::interrupt() {
  if (!iff1_)
    return;
  // Some dark business with parity flag here ignored.
  if (halted_) {
    halted_ = false;
    regs_.pc(regs_.pc() + 1);
  }
  iff1_ = iff2_ = false;
  pass_time(7);
  regs_.sp(regs_.sp() - 2);
  memory_.write16(regs_.sp(), regs_.pc());
  switch (irq_mode_) {
    case 0:
    case 1: regs_.pc(0x38); break;
    case 2: {
      // Assume the bus is at 0xff.
      const auto addr = static_cast<std::uint16_t>(0xff | (regs_.i() << 8));
      regs_.pc(memory_.read16(addr));
      break;
    }
    default: throw std::runtime_error("Inconceivable");
  }
}

} // namespace specbolt::v2
