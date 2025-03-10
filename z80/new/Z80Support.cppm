module;

export module z80:support;

import :z80;

namespace specbolt {

// TODO put these on the z80 itself and either:
// - split z80 into two for the in- and out-band
// - or update the Instruction etc code accordingly. as these model time.
export constexpr std::uint8_t read_immediate(Z80 &z80) {
  z80.pass_time(3);
  const auto addr = z80.regs().pc();
  z80.regs().pc(addr + 1);
  return z80.read8(addr);
}

export constexpr std::uint16_t read_immediate16(Z80 &z80) {
  const auto low = read_immediate(z80);
  const auto high = read_immediate(z80);
  return static_cast<std::uint16_t>(high << 8 | low);
}

export constexpr void write(Z80 &z80, const std::uint16_t address, const std::uint8_t byte) {
  z80.pass_time(3);
  z80.write8(address, byte);
}

export constexpr std::uint8_t read(Z80 &z80, const std::uint16_t address) {
  z80.pass_time(3);
  return z80.read8(address);
}

export constexpr std::uint8_t pop8(Z80 &z80) {
  const auto value = read(z80, z80.regs().sp());
  z80.regs().sp(z80.regs().sp() + 1);
  return value;
}

export constexpr std::uint16_t pop16(Z80 &z80) {
  const auto low = pop8(z80);
  const auto high = pop8(z80);
  return static_cast<std::uint16_t>(high << 8 | low);
}

export constexpr void push8(Z80 &z80, const std::uint8_t value) {
  z80.regs().sp(z80.regs().sp() - 1);
  write(z80, z80.regs().sp(), value);
}

export constexpr void push16(Z80 &z80, const std::uint16_t value) {
  push8(z80, static_cast<std::uint8_t>(value >> 8));
  push8(z80, static_cast<std::uint8_t>(value));
}


} // namespace specbolt
