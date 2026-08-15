#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/common/Z80Base.hpp"

#include <cstdint>
#include <functional>
#include <utility>

#include "peripherals/Memory.hpp"
#endif

namespace specbolt::v4 {

// What the outside world sees the CPU do. A machine may stretch or contend an
// access depending on its kind and address, so both reach `Z80::bus`, which is
// the only place in this CPU where time passes.
SPECBOLT_EXPORT enum class Bus : std::uint8_t {
  opcode, // instruction fetch: four cycles on the Z80, including the refresh
  operand, // a byte of the instruction following the opcode
  read, // data read
  write, // data write
  io_read, // IN: a separate address space, and a separate contention story
  io_write, // OUT
  internal, // no transfer at all, but the address bus still holds something
};

// The state a description may name, beyond the registers it inherits from
// `RegisterFile`. Each is an enum so that a flag bit, the halted state or the
// program counter can appear in a row exactly as `a` or `hl` does; a splice of
// one of these picks the matching `read` or `write` below by ordinary overload
// resolution, which is why the framework needs no idea what kind of location it
// is holding.
//
// They are enums even where there is only one of a thing, because a name is
// looked up by walking `enumerators_of` over each scope: a tag struct would be
// invisible to that search, and an enumerator is what carries the spelling a
// row writes.
//
// They are in a namespace of their own because that is what makes them
// locations. `Locations.hpp` walks this scope and nothing else, so an enum
// declared here is nameable by a description and one declared outside it is
// not, with no list to maintain and nothing to remember. `Bus` above is the
// case in point: its enumerators are bus cycle kinds, and a row has no business
// naming them.
namespace locations {

// Individually addressable flag bits, so `carry` is a location like any other.
// The ordinal is the bit position, which is what `read` shifts by; the
// static_asserts below hold that to what `Flags` actually says.
SPECBOLT_EXPORT enum class FlagBit : std::uint8_t { carry, subtract, parity, flag3, half_carry, flag5, zero, sign };

// Reordering the enumerators above would silently retarget every condition in
// the description: `jr nz` would test the wrong bit, with nothing to fail but
// the exerciser. These say so at compile time instead.
namespace detail {
constexpr bool flag_bit_is(const Flags flag, const FlagBit bit) {
  return flag.to_u8() == 1u << static_cast<unsigned>(bit);
}
} // namespace detail
static_assert(detail::flag_bit_is(Flags::Carry(), FlagBit::carry));
static_assert(detail::flag_bit_is(Flags::Subtract(), FlagBit::subtract));
static_assert(detail::flag_bit_is(Flags::Parity(), FlagBit::parity));
static_assert(detail::flag_bit_is(Flags::Flag3(), FlagBit::flag3));
static_assert(detail::flag_bit_is(Flags::HalfCarry(), FlagBit::half_carry));
static_assert(detail::flag_bit_is(Flags::Flag5(), FlagBit::flag5));
static_assert(detail::flag_bit_is(Flags::Zero(), FlagBit::zero));
static_assert(detail::flag_bit_is(Flags::Sign(), FlagBit::sign));

// The same register taken whole, distinct from R8::F so that only a
// Flags-shaped value can be written to it.
SPECBOLT_EXPORT enum class FlagWord : std::uint8_t { flags };

// The one-bit state the chip keeps outside any register. The two interrupt
// enables are flip-flops in Zilog's own words and HALT is one too; `deferred`
// is this emulator's, and behaves the same way.
SPECBOLT_EXPORT enum class FlipFlop : std::uint8_t { halted, iff1, iff2, deferred };

// The program counter, which is not in the programmer's register file.
SPECBOLT_EXPORT enum class ProgramCounter : std::uint8_t { pc };

// The high byte of the last address the machine formed. WZ, as the Z80
// literature calls it. `bit n, (ix+d)` takes flags 3 and 5 from it.
SPECBOLT_EXPORT enum class AddressLatch : std::uint8_t { wzh };

// How the chip is to answer an interrupt: `i` supplies the high byte of the
// vector in mode 2, and `im` is the mode itself. Only the ED table reaches
// either.
SPECBOLT_EXPORT enum class Interrupt : std::uint8_t { i, im };

// The memory refresh register, which the chip increments on every opcode fetch
// whether or not a description ever names it.
SPECBOLT_EXPORT enum class Refresh : std::uint8_t { r };

} // namespace locations

// So that the machine's own code says `FlagBit` rather than
// `locations::FlagBit`. A using-directive does not make these members of
// `specbolt::v4`, which is the whole point: `members_of` cannot see them
// through it, so they stay findable only by whoever walks `locations`.
using namespace locations;

// The index registers, named once rather than twice. A DD or FD prefix decodes
// the same table under a different *view*, and these are the locations that
// view selects between at run time, so `ix` and `iy` cost one description,
// one set of rows and one set of generated handlers between them. Each
// enumerator is named for the vocabulary in `z80.cpu` that resolves to it.
SPECBOLT_EXPORT class Z80 : public Z80Base {
public:
  explicit Z80(Scheduler &scheduler, Memory &memory) : Z80Base(scheduler, memory) {}

  void execute_one();
  // Run until `instructions` have started or the machine says stop. The
  // handlers tail-call each other for the whole of it, so this returns once.
  void run(std::size_t instructions);
  // The same, bounded by the clock rather than by a count, which is what a
  // scheduler wants: run to the next thing that is due.
  void run_until(std::size_t cycle_count);
  // Called between instructions by the generated code. Takes the interrupt,
  // idles a halted chip, and says whether there is another instruction to run.
  bool start_instruction();

  // What the framework asks of a machine. See refract/Machine.hpp. These are
  // the chip's own names for what it does; the framework calls them directly
  // rather than through anything in between.
  std::uint8_t fetch_opcode();
  std::uint16_t fetch_immediate(std::uint8_t width);
  [[nodiscard]] std::uint8_t read_memory(std::uint16_t address);
  [[nodiscard]] std::uint16_t read_memory16(std::uint16_t address);
  void write_memory(std::uint16_t address, std::uint8_t value);
  void write_memory16(std::uint16_t address, std::uint16_t value);
  void delay(std::uint8_t cycles);

  // How a displacement offsets a base, and what forming that address costs.
  // The Z80 sign-extends and spends a five-T-state window doing it, but any
  // immediate the instruction also carries is read *inside* that window, which
  // is why `ld (ix+d), n` is 19 T-states and not 22, and why the framework says
  // how many bytes it already read.
  [[nodiscard]] std::uint16_t displaced_address(std::uint16_t base, std::uint8_t offset, std::uint8_t immediate_bytes);

  // Reading and writing a named location. One overload per kind of location,
  // all called `read` or `write`, because the framework has only the one name
  // to call: it splices an enumerator and lets overload resolution land on the
  // right one. The return types differ, and that is the point: `carry` yields
  // a `bool` and `flags` a `Flags`, without anything in between being told.
  [[nodiscard]] std::uint8_t read(const RegisterFile::R8 location) const { return get(location); }
  [[nodiscard]] std::uint16_t read(const RegisterFile::R16 location) const { return get(location); }
  void write(const RegisterFile::R8 location, const std::uint8_t value) { set(location, value); }
  void write(const RegisterFile::R16 location, const std::uint16_t value) { set(location, value); }

  [[nodiscard]] bool read(const FlagBit which) const {
    return ((flags().to_u8() >> static_cast<unsigned>(which)) & 1u) != 0;
  }

  [[nodiscard]] Flags read(FlagWord) const { return flags(); }
  void write(FlagWord, const Flags value) { flags(value); }

  [[nodiscard]] bool read(const FlipFlop which) const {
    switch (which) {
      case FlipFlop::halted: return halted();
      case FlipFlop::iff1: return iff1();
      case FlipFlop::iff2: return iff2();
      case FlipFlop::deferred: return interrupts_deferred();
    }
    std::unreachable();
  }
  void write(const FlipFlop which, const bool value) {
    switch (which) {
      case FlipFlop::iff1: iff1(value); return;
      case FlipFlop::iff2: iff2(value); return;
      case FlipFlop::deferred: interrupts_deferred(value); return;
      case FlipFlop::halted: halted(value); return;
    }
  }

  [[nodiscard]] std::uint16_t read(ProgramCounter) const { return pc(); }
  void write(ProgramCounter, const std::uint16_t value) { regs().pc(value); }

  [[nodiscard]] std::uint8_t read(AddressLatch) const { return static_cast<std::uint8_t>(bus_address() >> 8); }

  [[nodiscard]] std::uint8_t read(const Interrupt which) const {
    return which == Interrupt::i ? regs().i() : irq_mode();
  }
  void write(const Interrupt which, const std::uint8_t value) {
    if (which == Interrupt::i)
      regs().i(value);
    else
      irq_mode(value);
  }

  [[nodiscard]] std::uint8_t read(Refresh) const { return regs().r(); }
  void write(Refresh, const std::uint8_t value) { regs().r(value); }

  // Advances time for one access, before the transfer happens, so anything
  // scheduled sees the machine as it was at that moment.
  void bus(Bus kind, std::uint16_t address);

  // What the address bus last held. Undocumented flags 3 and 5 come from here
  // on the instructions that have nothing better to give them.
  [[nodiscard]] std::uint16_t bus_address() const { return bus_address_; }

  using Z80Base::halted;
  void halted(bool value);

  // `ei` takes effect only after the instruction that follows it, so that
  // `ei ; halt` and `ei ; reti` do what they are written to do. The table says
  // an instruction defers; what deferring means is the machine's business.
  [[nodiscard]] bool interrupts_deferred() const { return interrupts_deferred_; }
  void interrupts_deferred(const bool value) { interrupts_deferred_ = value; }

private:
  std::size_t remaining_{};
  std::size_t until_{};

  std::uint8_t read_immediate();
  std::uint16_t read_immediate16();

  // Accepting an interrupt is not an instruction: no encoding matches it, so it
  // cannot be a row. It belongs to the machine that drives the decoder.
  void handle_interrupt();
  void refresh();

  // What the address bus last held, which is what an internal cycle presents.
  std::uint16_t bus_address_{};
  bool interrupts_deferred_{};
};

} // namespace specbolt::v4
