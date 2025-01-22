#include "Z80.hpp"

#include <stdexcept>

namespace specbolt {

// TODO flags registers and lookups therein.
// TODO consider making instructions out of composable objects? or templated functions? arg or is that too close to
//   the C++26 thing I want to demo?
void Z80::execute_one() {
  const auto opcode = read_and_inc_pc();
  pass_time(4);

  switch (opcode) {
    case 0x00: // NOP
      break;
    case 0x01: { // LD BC, nnnn
      regs_.set(RegisterFile::R16::BC, read16_and_inc_pc());
      pass_time(6);
      break;
    }
    case 0x11: { // LD DE, nnnn
      regs_.set(RegisterFile::R16::DE, read16_and_inc_pc());
      pass_time(6);
      break;
    }
    case 0xaf: { // XOR A, A
      regs_.set(RegisterFile::R8::A, 0);
      regs_.set(RegisterFile::R8::F, 0x80); // TODO look up in table[0];? not this anyway
      break;
    }
    case 0xf3: { // DI
      iff1_ = false;
      iff2_ = false;
      break;
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
