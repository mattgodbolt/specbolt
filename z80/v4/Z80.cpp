#include "z80/v4/Z80.hpp"

#include "Target.hpp"
#include "refract/Execute.hpp"

#include <utility>

namespace specbolt::v4 {

void Z80::run_until(const std::size_t cycle_count) {
  until_ = cycle_count;
  refract::Interpreter<Target>::run(*this);
}

// An instruction that starts before the deadline runs to its end, and the
// cheapest one takes longer than a cycle, so a deadline one cycle away is
// exactly one instruction. A halted chip spends a fetch per turn of the loop
// below, so it stops after one of those too.
void Z80::execute_one() { run_until(cycle_count() + 1); }

bool Z80::start_instruction() {
  // Called between instructions by the generated code, handles interrupts,
  // and loops for halted CPUs.
  // TODO: investigate how well the compiler can remove the likely true part
  // and whether it's profitable to have an inlined, non-looping version
  // deferring to the halting version out of line.
  while (true) {
    if (cycle_count() >= until_)
      return false;
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

// `execute_one` relies on this: a deadline one cycle away is one instruction
// only while every instruction passes time.
static_assert(cost_of(Bus::opcode) >= 1);

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

std::uint8_t Z80::fetch_immediate() {
  const auto address = regs_.pc();
  regs_.pc(static_cast<std::uint16_t>(address + 1));
  bus(Bus::operand, address);
  return memory_.read(address);
}

// An internal cycle presents whatever address the last access left on the bus,
// so a run of them re-latches the same value every time and only the clock
// actually moves. Spending them in one go is exactly equivalent, because
// `tick(n)` and n `tick(1)`s leave the same cycle count and fire the same tasks
// at the same cycles. It was worth 6% of v4 when it was measured, because
// `delay 7` had been seven calls.
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

// Low byte first, which is what `ld (nn), hl` does. A push does the opposite:
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

std::uint16_t Z80::fetch_immediate16() {
  const auto low = fetch_immediate();
  const auto high = fetch_immediate();
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


// ---------------------------------------------------------------------------
// The verbs marked as operations
// ---------------------------------------------------------------------------

void Z80::out_n(const std::uint8_t port, const std::uint8_t value) {
  const auto address = static_cast<std::uint16_t>(value << 8 | port);
  bus(Bus::io_write, address);
  out(address, value);
}

std::uint8_t Z80::in_n(const std::uint8_t port, const std::uint8_t high) {
  const auto address = static_cast<std::uint16_t>(high << 8 | port);
  bus(Bus::io_read, address);
  return in(address);
}

Alu::R8 Z80::in_c(const std::uint16_t port, const Flags flags) {
  bus(Bus::io_read, port);
  const auto value = in(port);
  return {value, Alu::parity_flags_for(value) | (flags & Flags::Carry())};
}

void Z80::out_c(const std::uint16_t port, const std::uint8_t value) {
  bus(Bus::io_write, port);
  out(port, value);
}

std::uint16_t Z80::ex_sp_hl(const std::uint16_t value) {
  const auto sp = regs_.sp();
  const auto low = read_memory(sp);
  const auto high = read_memory(static_cast<std::uint16_t>(sp + 1));
  delay(1);
  write_memory(static_cast<std::uint16_t>(sp + 1), static_cast<std::uint8_t>(value >> 8));
  write_memory(sp, static_cast<std::uint8_t>(value));
  delay(2);
  return static_cast<std::uint16_t>(high << 8 | low);
}

void Z80::exx() { regs_.exx(); }
void Z80::ex_de_hl() { regs_.ex(RegisterFile::R16::DE, RegisterFile::R16::HL); }
void Z80::ex_af() { regs_.ex(RegisterFile::R16::AF, RegisterFile::R16::AF_); }

Alu::R8 Z80::ld_a_special(const std::uint8_t value, const Flags flags) const {
  return {value, Alu::iff2_flags_for(value, flags, iff2())};
}

Alu::R8 Z80::nibble(const std::uint8_t value, const Flags flags, const bool right) {
  const auto a = regs_.get(RegisterFile::R8::A);
  const auto updated =
      static_cast<std::uint8_t>(right ? (a & 0xf0) | (value & 0x0f) : (a & 0xf0) | (value >> 4 & 0x0f));
  const auto written = static_cast<std::uint8_t>(right ? value >> 4 | (a & 0x0f) << 4 : value << 4 | (a & 0x0f));
  delay(4);
  regs_.set(RegisterFile::R8::A, updated);
  return {written, (flags & Flags::Carry()) | Alu::parity_flags_for(updated)};
}

Alu::R8 Z80::rrd8(const std::uint8_t value, const Flags flags) { return nibble(value, flags, true); }
Alu::R8 Z80::rld8(const std::uint8_t value, const Flags flags) { return nibble(value, flags, false); }

Flags Z80::counted(const Flags flags, const std::uint16_t bc, const std::uint8_t noise) {
  auto result = flags & ~(Flags::Subtract() | Flags::HalfCarry() | Flags::Overflow() | Flags::Flag3() | Flags::Flag5());
  if (bc != 1)
    result = result | Flags::Overflow();
  if (noise & 0x08)
    result = result | Flags::Flag3();
  if (noise & 0x02)
    result = result | Flags::Flag5();
  return result;
}

Flags Z80::stepped(const Flags flags) {
  const auto b = static_cast<std::uint8_t>(regs_.get(RegisterFile::R8::B) - 1);
  regs_.set(RegisterFile::R8::B, b);
  return Alu::parity_flags_for(b) | Flags::Subtract() | (flags & Flags::Carry());
}

Flags Z80::block_load(const BlockDirection direction, const Flags flags) {
  const auto step = static_cast<std::uint16_t>(direction == BlockDirection::Up ? 1 : 0xffff);
  const auto hl = regs_.get(RegisterFile::R16::HL);
  const auto de = regs_.get(RegisterFile::R16::DE);
  const auto bc = regs_.get(RegisterFile::R16::BC);
  const auto byte = read_memory(hl);
  write_memory(de, byte);
  delay(2);
  regs_.set(RegisterFile::R16::HL, static_cast<std::uint16_t>(hl + step));
  regs_.set(RegisterFile::R16::DE, static_cast<std::uint16_t>(de + step));
  regs_.set(RegisterFile::R16::BC, static_cast<std::uint16_t>(bc - 1));
  // Flags 3 and 5 come from the byte plus the accumulator, and swapped over.
  return counted(flags, bc, static_cast<std::uint8_t>(byte + regs_.get(RegisterFile::R8::A)));
}

Flags Z80::block_compare(const BlockDirection direction, const Flags flags) {
  const auto step = static_cast<std::uint16_t>(direction == BlockDirection::Up ? 1 : 0xffff);
  const auto hl = regs_.get(RegisterFile::R16::HL);
  const auto bc = regs_.get(RegisterFile::R16::BC);
  const auto byte = read_memory(hl);
  delay(5);
  regs_.set(RegisterFile::R16::HL, static_cast<std::uint16_t>(hl + step));
  regs_.set(RegisterFile::R16::BC, static_cast<std::uint16_t>(bc - 1));
  const auto compared = Alu::sub8(regs_.get(RegisterFile::R8::A), byte, false);
  // Flags 3 and 5 come from the difference, less one where it borrowed.
  const auto noise = static_cast<std::uint8_t>(compared.flags.half_carry() ? compared.result - 1 : compared.result);
  // The comparison's own sign, zero, half-carry and subtract go on last: the
  // count clears two of them, and a compare is entitled to say otherwise.
  constexpr auto compared_flags = Flags::HalfCarry() | Flags::Zero() | Flags::Sign() | Flags::Subtract();
  return (counted(flags, bc, noise) & ~compared_flags) | (compared.flags & compared_flags);
}

Flags Z80::block_in(const BlockDirection direction, const Flags flags) {
  delay(1);
  const auto port = regs_.get(RegisterFile::R16::BC);
  bus(Bus::io_read, port);
  const auto value = in(port);
  const auto hl = regs_.get(RegisterFile::R16::HL);
  write_memory(hl, value);
  regs_.set(RegisterFile::R16::HL, static_cast<std::uint16_t>(hl + (direction == BlockDirection::Up ? 1 : 0xffff)));
  return stepped(flags);
}

Flags Z80::block_out(const BlockDirection direction, const Flags flags) {
  delay(1);
  const auto hl = regs_.get(RegisterFile::R16::HL);
  const auto value = read_memory(hl);
  regs_.set(RegisterFile::R16::HL, static_cast<std::uint16_t>(hl + (direction == BlockDirection::Up ? 1 : 0xffff)));
  // B is counted down before the port goes on the bus, so it addresses with
  // the new value.
  const auto result = stepped(flags);
  const auto port = regs_.get(RegisterFile::R16::BC);
  bus(Bus::io_write, port);
  out(port, value);
  return result;
}

} // namespace specbolt::v4
