#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/common/Z80Base.hpp"

#include <cstdint>
#include <functional>

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

SPECBOLT_EXPORT class Z80 : public Z80Base {
public:
  explicit Z80(Scheduler &scheduler, Memory &memory) : Z80Base(scheduler, memory) {}

  void execute_one();

  std::uint8_t read_opcode();
  std::uint8_t read_immediate();
  std::uint16_t read_immediate16();

  [[nodiscard]] std::uint8_t read(std::uint16_t address);
  void write(std::uint16_t address, std::uint8_t value);
  void idle(std::uint8_t cycles);

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
  // Accepting an interrupt is not an instruction: no encoding matches it, so it
  // cannot be a row. It belongs to the machine that drives the decoder.
  void handle_interrupt();
  void refresh();

  // What the address bus last held, which is what an internal cycle presents.
  std::uint16_t bus_address_{};
  bool interrupts_deferred_{};
};

} // namespace specbolt::v4
