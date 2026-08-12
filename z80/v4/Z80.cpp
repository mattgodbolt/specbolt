#ifndef SPECBOLT_MODULES
#include "z80/v4/Z80.hpp"

#include "Execute.hpp"

#include <utility>
#endif

namespace specbolt::v4 {

void Z80::execute_one() {
  if (const auto deferred = std::exchange(interrupts_deferred_, false); irq_pending_ && !deferred) [[unlikely]]
    handle_interrupt();
  if (halted_) [[unlikely]] {
    pass_time(1);
    return;
  }
  execute_instruction(*this);
}

// The one sequence the table cannot describe: no opcode encodes it, and the
// byte it reads in mode 0 comes from the interrupting device rather than from
// memory. It is the machine's, not the instruction set's.
void Z80::handle_interrupt() {
  // The request is a level the device holds until it is acknowledged, so one
  // arriving while interrupts are off waits rather than being lost.
  if (!iff1_)
    return;
  irq_pending_ = false;
  if (halted_) {
    halted_ = false;
    // `halt` parks the program counter on itself; step off it.
    regs_.pc(static_cast<std::uint16_t>(regs_.pc() + 1));
  }
  iff1_ = iff2_ = false;
  // The acknowledge cycle: an opcode fetch stretched by two wait states, during
  // which the device would put a vector on the bus. Seven, then two writes,
  // makes the documented thirteen for modes 0 and 1 and nineteen for mode 2.
  idle(7);
  const auto return_to = regs_.pc();
  regs_.sp(static_cast<std::uint16_t>(regs_.sp() - 2));
  write(regs_.sp(), static_cast<std::uint8_t>(return_to));
  write(static_cast<std::uint16_t>(regs_.sp() + 1), static_cast<std::uint8_t>(return_to >> 8));
  switch (irq_mode_) {
    case 0: // Nothing drives the bus, so the byte reads as 0xff: `rst 0x38`.
    case 1: regs_.pc(0x38); break;
    case 2: {
      const auto vector = static_cast<std::uint16_t>(0xff | regs_.i() << 8);
      const auto low = read(vector);
      const auto high = read(static_cast<std::uint16_t>(vector + 1));
      regs_.pc(static_cast<std::uint16_t>(high << 8 | low));
      break;
    }
    default: break;
  }
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
