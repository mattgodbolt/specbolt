#include "Z80.hpp"

#include <stdexcept>

namespace specbolt {

void Z80::execute_one() {
  const auto opcode = read_and_inc_pc();
  switch (opcode) {
    case 0x00: // NOP
      break;
    case 0x01: {
      // LD BC, nnnn
      regs_.set(RegisterFile::R16::BC, read16_and_inc_pc());
      pass_time(6);
    }
    default:
      throw std::runtime_error("bad opcode");
  }
}

std::uint8_t Z80::read_and_inc_pc() {
  const auto pc = regs_.pc();
  regs_.pc(pc + 1);
  return memory_.read(pc);
}

std::uint16_t Z80::read16_and_inc_pc() {
  const auto low = read_and_inc_pc();
  const auto high = read_and_inc_pc();
  return static_cast<std::uint16_t>(high << 8) | low;
}

void Z80::pass_time(size_t tstates) { now_tstates_ += tstates; }

} // namespace specbolt
