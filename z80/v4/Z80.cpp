#ifndef SPECBOLT_MODULES
#include "z80/v4/Z80.hpp"

#include "refract/Execute.hpp"

#include <limits>
#include <utility>
#endif

namespace specbolt::v4 {

using refract::execute_instruction;

void Z80::execute_one() { run(1); }

void Z80::run(const std::size_t instructions) {
  remaining_ = instructions;
  until_ = std::numeric_limits<std::size_t>::max();
  execute_instruction(*this);
}

void Z80::run_until(const std::size_t cycle_count) {
  remaining_ = std::numeric_limits<std::size_t>::max();
  until_ = cycle_count;
  execute_instruction(*this);
}

bool Z80::start_instruction() {
  // A loop rather than a test, because a halted chip consumes instructions
  // without executing any, and the run has to end whether it wakes or not.
  while (true) {
    if (remaining_ == 0 || cycle_count() >= until_)
      return false;
    --remaining_;
    if (const auto deferred = std::exchange(interrupts_deferred_, false); irq_pending_ && !deferred) [[unlikely]]
      handle_interrupt();
    if (!halted_) [[likely]]
      return true;
    // A halted Z80 is executing internal NOPs, not stopped: it still fetches
    // at the address it parked on, without advancing, so it still spends an
    // opcode cycle on the bus and still refreshes.
    bus(Bus::opcode, pc());
    refresh();
  }
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
  delay(7);
  // The acknowledge is an M1 cycle, and every M1 refreshes.
  refresh();
  const auto return_to = regs_.pc();
  regs_.sp(static_cast<std::uint16_t>(regs_.sp() - 2));
  write_memory(regs_.sp(), static_cast<std::uint8_t>(return_to));
  write_memory(static_cast<std::uint16_t>(regs_.sp() + 1), static_cast<std::uint8_t>(return_to >> 8));
  switch (irq_mode_) {
    case 0: // Nothing drives the bus, so the byte reads as 0xff: `rst 0x38`.
    case 1: regs_.pc(0x38); break;
    case 2: {
      const auto vector = static_cast<std::uint16_t>(0xff | regs_.i() << 8);
      const auto low = read_memory(vector);
      const auto high = read_memory(static_cast<std::uint16_t>(vector + 1));
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

// The refresh counter is seven bits; the top bit is whatever was last written
// to it and does not count.
void Z80::refresh() { regs_.r(static_cast<std::uint8_t>((regs_.r() & 0x80) | ((regs_.r() + 1) & 0x7f))); }

std::uint8_t Z80::fetch_opcode() {
  const auto address = regs_.pc();
  regs_.pc(static_cast<std::uint16_t>(address + 1));
  bus(Bus::opcode, address);
  refresh();
  return memory_.read(address);
}

std::uint16_t Z80::fetch_immediate(const std::uint8_t width) {
  return width == 1 ? read_immediate() : read_immediate16();
}

std::uint8_t Z80::read_immediate() {
  const auto address = regs_.pc();
  regs_.pc(static_cast<std::uint16_t>(address + 1));
  bus(Bus::operand, address);
  return memory_.read(address);
}

// An internal cycle presents whatever address the last access left on the bus,
// so a run of them re-latches the same value every time and only the clock
// actually moves. Spending them in one go is exactly equivalent, because `tick(n)`
// and n `tick(1)`s leave the same cycle count and fire the same tasks at the
// covers the same cycles, and it is worth 6% of v4, because `delay 7` was seven calls.
//
// This is the line contention will have to undo. A contended machine can
// stretch each internal cycle separately, so it would want the loop back, with
// `bus` deciding what each individual cycle costs.
void Z80::delay(const std::uint8_t cycles) { pass_time(cycles * cost_of(Bus::internal)); }

std::uint16_t Z80::read_memory16(const std::uint16_t address) {
  // Two accesses, low byte first, because that is what the bus sees.
  const auto low = read_memory(address);
  return static_cast<std::uint16_t>(read_memory(static_cast<std::uint16_t>(address + 1)) << 8 | low);
}

// Low byte first, which is what `ld (nn), hl` does. A push does the opposite --
// high byte to sp-1, then low to sp-2, and gets this order instead. The bytes
// land in the same places either way, so nothing can see the difference until
// `bus` starts contending or a watchpoint watches. Recorded in NOTES.
void Z80::write_memory16(const std::uint16_t address, const std::uint16_t value) {
  write_memory(address, static_cast<std::uint8_t>(value));
  write_memory(static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(value >> 8));
}

// The window is five cycles, less the three each already-read immediate spent
// inside it, so at most one byte can have been read: two would underflow. The
// generator proves that with a `static_assert` before it ever calls this.
std::uint16_t Z80::displaced_address(
    const std::uint16_t base, const std::uint8_t offset, const std::uint8_t immediate_bytes) {
  delay(static_cast<std::uint8_t>(5 - 3 * immediate_bytes));
  return static_cast<std::uint16_t>(base + static_cast<std::int8_t>(offset));
}

std::uint16_t Z80::read_immediate16() {
  const auto low = read_immediate();
  const auto high = read_immediate();
  return static_cast<std::uint16_t>(high << 8 | low);
}

std::uint8_t Z80::read_memory(const std::uint16_t address) {
  bus(Bus::read, address);
  return memory_.read(address);
}

void Z80::write_memory(const std::uint16_t address, const std::uint8_t value) {
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
