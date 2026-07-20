#include "z80/v4/Z80.hpp"

namespace specbolt::v4 {

namespace {
constexpr std::uint8_t kPrefixCB = 0xcb;
constexpr std::uint8_t kPrefixDD = 0xdd;
constexpr std::uint8_t kPrefixED = 0xed;
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

  switch (opcode) {
    case kPrefixCB: {
      regs_.r(static_cast<std::uint8_t>((regs_.r() & 0x80) | ((regs_.r() + 1) & 0x7f)));
      const auto inner = read_immediate();
      pass_time(1);
      regs_.wz(regs_.get(RegisterFile::R16::HL));
      dispatch_cb(inner);
      break;
    }
    case kPrefixED: {
      regs_.r(static_cast<std::uint8_t>((regs_.r() & 0x80) | ((regs_.r() + 1) & 0x7f)));
      const auto inner = read_immediate();
      pass_time(1);
      dispatch_ed(inner);
      break;
    }
    case kPrefixDD:
    case kPrefixFD: {
      regs_.r(static_cast<std::uint8_t>((regs_.r() & 0x80) | ((regs_.r() + 1) & 0x7f)));
      const auto inner = read_immediate();
      pass_time(1);
      if (inner == kPrefixCB) {
        // DD CB d xx / FD CB d xx — displacement before opcode.
        const auto disp = static_cast<std::int8_t>(read_immediate());
        const auto index = (opcode == kPrefixDD) ? regs_.get(RegisterFile::R16::IX)
                                                  : regs_.get(RegisterFile::R16::IY);
        regs_.wz(static_cast<std::uint16_t>(index + disp));
        pass_time(2);
        const auto inner2 = read_immediate();
        dispatch_idxcb(inner2);
      } else if (opcode == kPrefixDD) {
        dispatch_dd(inner);
      } else {
        dispatch_fd(inner);
      }
      break;
    }
    default:
      dispatch_base(opcode);
      break;
  }
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

std::uint8_t Z80::pop8() {
  const auto value = read(regs_.sp());
  regs_.sp(static_cast<std::uint16_t>(regs_.sp() + 1));
  return value;
}

std::uint16_t Z80::pop16() {
  const auto lo = pop8();
  const auto hi = pop8();
  return static_cast<std::uint16_t>(hi << 8 | lo);
}

void Z80::push8(const std::uint8_t value) {
  regs_.sp(static_cast<std::uint16_t>(regs_.sp() - 1));
  write(regs_.sp(), value);
}

void Z80::push16(const std::uint16_t value) {
  push8(static_cast<std::uint8_t>(value >> 8));
  push8(static_cast<std::uint8_t>(value));
}

} // namespace specbolt::v4
