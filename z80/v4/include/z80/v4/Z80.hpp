#pragma once

#include "refract/Model.hpp"
#include "z80/common/Alu.hpp"
#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/common/Z80Base.hpp"

#include <cstdint>
#include <utility>

#include "peripherals/Memory.hpp"

namespace specbolt::v4 {

// What the outside world sees the CPU do: the kind of each access, which a machine may stretch or contend by kind and
// address. Every access reaches `Z80::bus`; idle cycles reach `Z80::delay`.
SPECBOLT_EXPORT enum class Bus : std::uint8_t {
  opcode, // instruction fetch: four cycles on the Z80, including the refresh
  operand, // a byte of the instruction following the opcode
  read, // data read
  write, // data write
  io_read, // IN: a separate address space, and a separate contention story
  io_write, // OUT
  internal, // no transfer at all, but the address bus still holds something
};

// The state a description may name beyond the registers of `RegisterFile` and the bits of `Flags::Bit`. Each is an enum
// so that a row can write `halted` or `pc` exactly as it writes `a`: the enum a name is found in says which of the
// marked `read` and `write` members below reaches it, and the framework needs to know nothing else about it. A name is
// found by walking `enumerators_of`, so even a lone location is an enumerator.

// The flags register taken whole, distinct from R8::F so that only a Flags-shaped value can be written to it.
SPECBOLT_EXPORT enum class FlagWord : std::uint8_t { flags };

// The one-bit state the chip keeps outside any register. The two interrupt enables are flip-flops in Zilog's own words
// and HALT is one too; `deferred` is this emulator's, and behaves the same way.
SPECBOLT_EXPORT enum class FlipFlop : std::uint8_t { halted, iff1, iff2, deferred };

// The program counter, which is not in the programmer's register file.
SPECBOLT_EXPORT enum class ProgramCounter : std::uint8_t { pc };

// The high byte of the last address on the bus, standing in for the high byte of WZ (MEMPTR), which is not modelled.
// `bit n, (ix+d)` takes flags 3 and 5 from it.
SPECBOLT_EXPORT enum class AddressLatch : std::uint8_t { wzh };

// How the chip is to answer an interrupt: `i` supplies the high byte of the vector in mode 2, and `im` is the mode
// itself. Only the ED table reaches either.
SPECBOLT_EXPORT enum class Interrupt : std::uint8_t { i, im };

// The memory refresh register, which the chip increments on every opcode fetch whether or not a description ever names
// it.
SPECBOLT_EXPORT enum class Refresh : std::uint8_t { r };

// Which way the block operations walk memory. The annotations are what `z80.cpu` calls each direction: `i` and `d`, the
// letters `ldi` and `ldd` end in. The description names a direction; nothing depends on their numeric values.
SPECBOLT_EXPORT enum class BlockDirection : std::uint8_t {
  Up[[= refract::Spelling{"i"}]],
  Down[[= refract::Spelling{"d"}]],
};

// The Z80 as refract drives it: the state a description may name, the verbs the chip marks as its own, and what the
// framework asks of a machine.
SPECBOLT_EXPORT class Z80 : public Z80Base {
public:
  explicit Z80(Scheduler &scheduler, Memory &memory) : Z80Base(scheduler, memory) {}

  // Runs until the clock reaches `cycle_count` or the machine says stop, which is what a scheduler wants: run to the
  // next thing that is due. The handlers tail-call each other for the whole of it, so this returns once.
  void run_until(std::size_t cycle_count);
  // Runs one instruction, however long it takes.
  void execute_one();
  // Takes a pending interrupt, idles a halted chip, and says whether there is another instruction to run. Called
  // between instructions by the generated code.
  [[nodiscard]] bool start_instruction();

  // What the framework asks of a machine; see refract/Machine.hpp. The framework calls these directly, under the chip's
  // own names. The fetches are defined here because one runs for every byte of every instruction, and their being
  // inline was measured to matter (notes/MEASUREMENTS.md).
  [[nodiscard]] std::uint8_t fetch_opcode() {
    const auto address = regs_.pc();
    regs_.pc(static_cast<std::uint16_t>(address + 1));
    bus(Bus::opcode, address);
    refresh();
    return memory_.read(address);
  }
  [[nodiscard]] std::uint8_t fetch_immediate() {
    const auto address = regs_.pc();
    regs_.pc(static_cast<std::uint16_t>(address + 1));
    bus(Bus::operand, address);
    return memory_.read(address);
  }
  [[nodiscard]] std::uint8_t read_memory(std::uint16_t address);
  [[nodiscard]] std::uint16_t read_memory16(std::uint16_t address);
  void write_memory(std::uint16_t address, std::uint8_t value);
  void write_memory16(std::uint16_t address, std::uint16_t value);
  // Spends `cycles` idle cycles. Also a verb: `delay 2` is a step a row may write.
  [[= refract::operation]] void delay(std::uint8_t cycles);

  // The verbs a description may name on the chip itself, marked one by one: what the rows cannot express as operands
  // because it touches the machine in an order or a place no operand can. The verbs that touch no chip are in
  // Operations.hpp.

  // `out (n),a` and `in a,(n)`. The port is sixteen bits wide even when the encoding writes eight: the Z80 puts the
  // accumulator on the top half.
  [[= refract::operation]] void out_n(std::uint8_t port, std::uint8_t value);
  [[nodiscard]][[= refract::operation]] std::uint8_t in_n(std::uint8_t port, std::uint8_t high);
  // `in r,(c)` addresses with the whole of bc and sets flags; `out (c),r` does not. Neither is expressible as an
  // operand, because the port space is not memory. Sign, zero and parity come from the byte; the carry is explicitly
  // *not* affected, so it is carried through rather than recomputed.
  [[nodiscard]][[= refract::operation]] Alu::R8 in_c(std::uint16_t port, Flags flags);
  [[= refract::operation]] void out_c(std::uint16_t port, std::uint8_t value);

  // `ex (sp),hl`: swaps `value` with the word at sp and returns the old word, in nineteen T-states with the fetch: two
  // reads, an idle cycle, two writes, then two more idle cycles. No row could write that interleaving as operands.
  [[nodiscard]][[= refract::operation]] std::uint16_t ex_sp_hl(std::uint16_t value);
  // The exchanges move whole register pairs about, which no operand can name.
  [[= refract::operation]] void exx();
  [[= refract::operation]] void ex_de_hl();
  [[= refract::operation]] void ex_af();

  // `ld a,i` and `ld a,r` report iff2 in the parity flag, which is the one way a program can see the interrupt state.
  [[nodiscard]][[= refract::operation]] Alu::R8 ld_a_special(std::uint8_t value, Flags flags) const;

  // `rrd` and `rld` move a nibble between the accumulator and memory, so both ends change at once and only one of them
  // can be a destination.
  [[nodiscard]][[= refract::operation]] Alu::R8 rrd8(std::uint8_t value, Flags flags);
  [[nodiscard]][[= refract::operation]] Alu::R8 rld8(std::uint8_t value, Flags flags);

  // The block operations move or compare one byte, step hl (and de), and count down: bc for the loads and compares, b
  // for the in and out forms. The repeating forms are the same row with a condition and a rewind: the chip really does
  // re-execute the opcode, which is why an interrupt can land in the middle of an `ldir`.
  [[nodiscard]][[= refract::operation]] Flags block_load(BlockDirection direction, Flags flags);
  [[nodiscard]][[= refract::operation]] Flags block_compare(BlockDirection direction, Flags flags);
  [[nodiscard]][[= refract::operation]] Flags block_in(BlockDirection direction, Flags flags);
  [[nodiscard]][[= refract::operation]] Flags block_out(BlockDirection direction, Flags flags);

  // How a displacement offsets a base, and what forming that address costs. The Z80 spends a five-T-state window doing
  // it, but any immediate the instruction also carries is read *inside* that window, which is
  // why `ld (ix+d), n` is 19 T-states and not 22, and why the framework says how many bytes it already read. Three per
  // byte, so the window holds one; the framework checks every displaced row against that, and reports one that reads
  // more.
  static constexpr std::uint8_t displacement_window_bytes = 1;
  [[nodiscard]] std::uint16_t displaced_address(
      const std::uint16_t base, const std::int8_t offset, const std::uint8_t bytes_read) {
    delay(static_cast<std::uint8_t>(5 - 3 * bytes_read));
    return static_cast<std::uint16_t>(base + offset);
  }

  // Reading and writing a named location, a pair per kind of location. The marks are what publish each enum's
  // enumerators to descriptions and tell the framework which member to call; the names are this class's own.
  // `Flags::Bit` and `AddressLatch` have no write, so a row writing `carry` or `wzh` is an error against its line;
  // nothing is marked for `Bus`, so `opcode` and the rest are not names a row can write.
  [[nodiscard]][[= refract::location.read]] std::uint8_t read(const RegisterFile::R8 location) const {
    return get(location);
  }
  [[nodiscard]][[= refract::location.read]] std::uint16_t read(const RegisterFile::R16 location) const {
    return get(location);
  }
  [[= refract::location.write]] void write(const RegisterFile::R8 location, const std::uint8_t value) {
    set(location, value);
  }
  [[= refract::location.write]] void write(const RegisterFile::R16 location, const std::uint16_t value) {
    set(location, value);
  }

  [[nodiscard]][[= refract::location.read]] bool read(const Flags::Bit which) const { return flags().test(which); }

  [[nodiscard]][[= refract::location.read]] Flags read(FlagWord) const { return flags(); }
  [[= refract::location.write]] void write(FlagWord, const Flags value) { flags(value); }

  [[nodiscard]][[= refract::location.read]] bool read(const FlipFlop which) const {
    switch (which) {
      case FlipFlop::halted: return halted();
      case FlipFlop::iff1: return iff1();
      case FlipFlop::iff2: return iff2();
      case FlipFlop::deferred: return interrupts_deferred();
    }
    std::unreachable();
  }
  [[= refract::location.write]] void write(const FlipFlop which, const bool value) {
    switch (which) {
      case FlipFlop::iff1: iff1(value); return;
      case FlipFlop::iff2: iff2(value); return;
      case FlipFlop::deferred: interrupts_deferred(value); return;
      case FlipFlop::halted: halted(value); return;
    }
  }

  [[nodiscard]][[= refract::location.read]] std::uint16_t read(ProgramCounter) const { return pc(); }
  [[= refract::location.write]] void write(ProgramCounter, const std::uint16_t value) { regs().pc(value); }

  [[nodiscard]][[= refract::location.read]] std::uint8_t read(AddressLatch) const {
    return static_cast<std::uint8_t>(bus_address() >> 8);
  }

  [[nodiscard]][[= refract::location.read]] std::uint8_t read(const Interrupt which) const {
    return which == Interrupt::i ? regs().i() : irq_mode();
  }
  [[= refract::location.write]] void write(const Interrupt which, const std::uint8_t value) {
    if (which == Interrupt::i)
      regs().i(value);
    else
      irq_mode(value);
  }

  [[nodiscard]][[= refract::location.read]] std::uint8_t read(Refresh) const { return regs().r(); }
  [[= refract::location.write]] void write(Refresh, const std::uint8_t value) { regs().r(value); }

  // Advances time for one access, before the transfer happens, so anything scheduled sees the machine as it was at that
  // moment.
  void bus(Bus kind, std::uint16_t address);

  // What the address bus last held. Undocumented flags 3 and 5 come from here on the instructions that have nothing
  // better to give them.
  [[nodiscard]] std::uint16_t bus_address() const { return bus_address_; }

  using Z80Base::halted;
  // Halts the chip, or wakes it. Halting goes through `halt()`, which parks the program counter on the instruction as
  // well as setting the flag; waking only clears the flag, since whoever wakes the chip steps the counter off it.
  void halted(bool value);

  // Whether the interrupt just enabled is held off until one more instruction has run. `ei` takes effect only after the
  // instruction that follows it, so that `ei ; halt` and `ei ; reti` do what they are written to do. The table says an
  // instruction defers; what deferring means is the machine's business.
  [[nodiscard]] bool interrupts_deferred() const { return interrupts_deferred_; }
  void interrupts_deferred(const bool value) { interrupts_deferred_ = value; }

private:
  std::size_t until_{};

  // Accepts the pending interrupt: pushes the return address and jumps where the mode says. Accepting one is not an
  // instruction, so it cannot be a row: no opcode encodes it, and the byte it reads in mode 0 comes from the
  // interrupting device rather than from memory. It belongs to the machine that drives the decoder.
  void handle_interrupt();
  // Steps the refresh counter, as every M1 cycle does.
  void refresh();
  // Reading and writing a port: an I/O cycle on the bus, then the transfer, as `read_memory` and `write_memory` are for
  // memory.
  [[nodiscard]] std::uint8_t read_port(std::uint16_t port);
  void write_port(std::uint16_t port, std::uint8_t value);

  // What the address bus last held, which is what an internal cycle presents.
  std::uint16_t bus_address_{};
  bool interrupts_deferred_{};

  // What every block operation does to the flags it does not otherwise touch: parity stands in for "bc has not run
  // out", and flag 3 comes from bit 3 and flag 5 from bit 1 of `noise`, a value the instruction happens to have to
  // hand.
  [[nodiscard]] static Flags counted(Flags flags, std::uint16_t bc, std::uint8_t noise);
  // Counts b down for an in or out block form and returns the flags that leaves. Those forms count b rather than bc.
  // Their real parity comes from `(value + ((c ± 1) & 0xff)) & 7` exclusive-ored with b, and their half carry and carry
  // from whether that sum passed 255; none of that is modelled, so only sign, zero and flags 3 and 5 are trustworthy
  // here.
  [[nodiscard]] Flags stepped(Flags flags);
  // Rotates a nibble between `a` and `value` for `rrd` and `rld`, changing `a` in place and returning what goes back to
  // memory with the flags. `Right` is `rrd`: the low nibble of `value` goes into `a`, `value`'s high nibble drops to
  // the low half of what is written back, and `a`'s old low nibble fills the high half. `Left` is `rld`, the other way:
  // the high nibble of `value` goes into `a`, `value`'s low nibble rises to the high half, and `a`'s old low nibble
  // fills the low half.
  [[nodiscard]] Alu::R8 nibble(std::uint8_t value, Flags flags, Alu::Direction direction);
};

} // namespace specbolt::v4
