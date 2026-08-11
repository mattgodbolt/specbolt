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

namespace {

// What each kind of access costs on a Z80 with nothing extending it.
[[nodiscard]] constexpr std::size_t cost_of(const Bus kind) {
  switch (kind) {
    case Bus::opcode: return 4;
    case Bus::operand:
    case Bus::read:
    case Bus::write: return 3;
    case Bus::io_read:
    case Bus::io_write: return 4; // three, plus the wait state the Z80 always inserts
    case Bus::internal: return 1;
  }
  return 0;
}

} // namespace

void Z80::bus(const Bus kind, const std::uint16_t address) {
  // A machine that contends or stretches does it here, from the kind, the
  // address and the position within the frame. The Spectrum contends
  // 0x4000-0x7fff while the display is being drawn; nothing models that yet.
  // TODO: pass_time(contention(kind, address, cycle_count()));
  pass_time(cost_of(kind));
  bus_address_ = address;
}

std::uint8_t Z80::read_opcode() {
  const auto address = regs_.pc();
  regs_.pc(static_cast<std::uint16_t>(address + 1));
  bus(Bus::opcode, address);
  regs_.r((regs_.r() & 0x80) | ((regs_.r() + 1) & 0x7f));
  return memory_.read(address);
}

std::uint8_t Z80::read_immediate() {
  const auto address = regs_.pc();
  regs_.pc(static_cast<std::uint16_t>(address + 1));
  bus(Bus::operand, address);
  return memory_.read(address);
}

void Z80::idle(const std::uint8_t cycles) {
  for (std::uint8_t at = 0; at < cycles; ++at)
    bus(Bus::internal, bus_address_);
}

std::uint16_t Z80::read_immediate16() {
  const auto low = read_immediate();
  const auto high = read_immediate();
  return static_cast<std::uint16_t>(high << 8 | low);
}

std::uint8_t Z80::read(const std::uint16_t address) {
  bus(Bus::read, address);
  return memory_.read(address);
}

void Z80::write(const std::uint16_t address, const std::uint8_t value) {
  bus(Bus::write, address);
  memory_.write(address, value);
}

void Z80::halted(const bool value) {
  if (value)
    halt();
  else
    halted_ = false;
}

} // namespace specbolt::v4
