#ifndef SPECBOLT_MODULES
#include "z80/v4/Z80.hpp"

#include "Execute.hpp"
#endif

namespace specbolt::v4 {

void Z80::execute_one() {
  if (halted_) [[unlikely]] {
    pass_time(1);
    return;
  }
  enter<entry_table>(*this);
}

std::uint8_t Z80::read_opcode() {
  const auto opcode = read_immediate();
  regs_.r((regs_.r() & 0x80) | ((regs_.r() + 1) & 0x7f));
  pass_time(1);
  return opcode;
}

std::uint8_t Z80::read_immediate() {
  pass_time(3);
  const auto address = regs_.pc();
  regs_.pc(static_cast<std::uint16_t>(address + 1));
  return memory_.read(address);
}

std::uint16_t Z80::read_immediate16() {
  const auto low = read_immediate();
  const auto high = read_immediate();
  return static_cast<std::uint16_t>(high << 8 | low);
}

std::uint8_t Z80::read(const std::uint16_t address) {
  pass_time(3);
  return memory_.read(address);
}

void Z80::write(const std::uint16_t address, const std::uint8_t value) {
  pass_time(3);
  memory_.write(address, value);
}

void Z80::halted(const bool value) {
  if (value)
    halt();
  else
    halted_ = false;
}

} // namespace specbolt::v4
