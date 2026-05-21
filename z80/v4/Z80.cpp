#include "z80/v4/Z80.hpp"

namespace specbolt::v4 {

namespace {
constexpr std::uint8_t kPrefixDD = 0xdd;
constexpr std::uint8_t kPrefixFD = 0xfd;
} // namespace

void Z80::execute_one() {
  if (halted_) [[unlikely]] {
    pass_time(1);
    return;
  }
  // R refresh: bottom 7 bits increment, top bit preserved.
  regs_.r(static_cast<std::uint8_t>((regs_.r() & 0x80) | ((regs_.r() + 1) & 0x7f)));
  const auto opcode = read_immediate();
  pass_time(1); // last cycle of M1.

  if (opcode == kPrefixDD || opcode == kPrefixFD) [[unlikely]] {
    // Second M1 cycle for the prefixed opcode.
    regs_.r(static_cast<std::uint8_t>((regs_.r() & 0x80) | ((regs_.r() + 1) & 0x7f)));
    const auto inner = read_immediate();
    pass_time(1);
    if (opcode == kPrefixDD) dispatch_dd(inner);
    else dispatch_fd(inner);
    return;
  }
  dispatch_base(opcode);
}

std::uint8_t Z80::read_immediate() {
  pass_time(3);
  const auto pc = regs_.pc();
  regs_.pc(static_cast<std::uint16_t>(pc + 1));
  return memory_.read(pc);
}

std::uint16_t Z80::read_immediate16() {
  const auto lo = read_immediate();
  const auto hi = read_immediate();
  return static_cast<std::uint16_t>(hi << 8 | lo);
}

std::uint8_t Z80::read(const std::uint16_t address) {
  pass_time(3);
  return memory_.read(address);
}

void Z80::write(const std::uint16_t address, const std::uint8_t byte) {
  pass_time(3);
  memory_.write(address, byte);
}

} // namespace specbolt::v4
