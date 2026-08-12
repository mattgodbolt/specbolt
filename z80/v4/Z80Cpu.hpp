#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Alu.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/v4/Z80.hpp"

#include <array>
#include <cstdint>
#include <meta>
#endif

// Everything the framework needs to know about the Z80, and nothing it needs to
// know about any other CPU. Retargeting means writing one of these.

namespace specbolt::v4 {

using Cpu = Z80;

inline void delay(Cpu &cpu, const std::uint8_t cycles) { cpu.idle(cycles); }

struct Ops {
  static void nop() {}
  static std::uint16_t ld16(const std::uint16_t value) { return value; }
  static std::uint16_t inc16(const std::uint16_t value) { return static_cast<std::uint16_t>(value + 1); }
  static std::uint16_t dec16(const std::uint16_t value) { return static_cast<std::uint16_t>(value - 1); }
  static std::uint8_t ld8(const std::uint8_t value) { return value; }
  static void delay(Cpu &cpu, const std::uint8_t cycles) { specbolt::v4::delay(cpu, cycles); }
  // Alu::bit takes a mask; the encoding carries an index, as res and set do.
  // Flags 3 and 5 come from whatever was last on the bus, which the row names.
  static Flags bit8(const std::uint8_t value, const std::uint8_t bit, const Flags flags, const std::uint8_t bus) {
    return Alu::bit(value, static_cast<std::uint8_t>(1u << bit), flags, bus);
  }
  static std::uint8_t res8(const std::uint8_t value, const std::uint8_t bit) {
    return static_cast<std::uint8_t>(value & ~(1u << bit));
  }
  static std::uint8_t set8(const std::uint8_t value, const std::uint8_t bit) {
    return static_cast<std::uint8_t>(value | 1u << bit);
  }

  // The rotate and shift family. `Alu` spells the direction as an argument;
  // a table names operations, so each direction gets a name.
  static Alu::R8 rlc8(const std::uint8_t v) { return Alu::rotate_circular8(v, Alu::Direction::Left); }
  static Alu::R8 rrc8(const std::uint8_t v) { return Alu::rotate_circular8(v, Alu::Direction::Right); }
  static Alu::R8 rl8(const std::uint8_t v, const bool carry) { return Alu::rotate8(v, Alu::Direction::Left, carry); }
  static Alu::R8 rr8(const std::uint8_t v, const bool carry) { return Alu::rotate8(v, Alu::Direction::Right, carry); }
  static Alu::R8 sla8(const std::uint8_t v) { return Alu::shift_arithmetic8(v, Alu::Direction::Left); }
  static Alu::R8 sra8(const std::uint8_t v) { return Alu::shift_arithmetic8(v, Alu::Direction::Right); }
  // Undocumented, and shifts a one in where sla shifts a zero.
  static Alu::R8 sll8(const std::uint8_t v) { return Alu::shift_logical8(v, Alu::Direction::Left); }
  static Alu::R8 srl8(const std::uint8_t v) { return Alu::shift_logical8(v, Alu::Direction::Right); }

  // The accumulator forms keep sign, zero and parity rather than recomputing
  // them, which is the whole difference between `rlca` and `rlc a`.
  static Alu::R8 rlca8(const std::uint8_t v, const Flags f) {
    return Alu::fast_rotate_circular8(v, Alu::Direction::Left, f);
  }
  static Alu::R8 rrca8(const std::uint8_t v, const Flags f) {
    return Alu::fast_rotate_circular8(v, Alu::Direction::Right, f);
  }
  static Alu::R8 rla8(const std::uint8_t v, const Flags f) { return Alu::fast_rotate8(v, Alu::Direction::Left, f); }
  static Alu::R8 rra8(const std::uint8_t v, const Flags f) { return Alu::fast_rotate8(v, Alu::Direction::Right, f); }

  // Conditions. A vocabulary member binds one of these and appends the flag it
  // asks about, exactly as `adc` binds `add8` and appends the carry.
  static bool is_set(const bool flag) { return flag; }
  static bool is_clear(const bool flag) { return !flag; }
  static bool nonzero(const std::uint8_t value) { return value != 0; }

  // `djnz` counts without touching the flags, which `dec8` would.
  static std::uint8_t dec8_quiet(const std::uint8_t value) { return static_cast<std::uint8_t>(value - 1); }

  // A relative jump is measured from the byte after the offset, which is where
  // the program counter already is.
  static std::uint16_t relative(const std::uint16_t pc, const std::uint8_t offset) {
    return static_cast<std::uint16_t>(pc + static_cast<std::int8_t>(offset));
  }

  // The port is sixteen bits wide even when the encoding writes eight: the Z80
  // puts the accumulator on the top half.
  static void out_n(Cpu &cpu, const std::uint8_t port, const std::uint8_t value) {
    const auto address = static_cast<std::uint16_t>(value << 8 | port);
    cpu.bus(Bus::io_write, address);
    cpu.out(address, value);
  }
  static std::uint8_t in_n(Cpu &cpu, const std::uint8_t port, const std::uint8_t high) {
    const auto address = static_cast<std::uint16_t>(high << 8 | port);
    cpu.bus(Bus::io_read, address);
    return cpu.in(address);
  }

  // Three accesses and two idle stretches, none of which an operand can spell.
  static std::uint16_t ex_sp_hl(Cpu &cpu, const std::uint16_t value) {
    const auto sp = cpu.get(RegisterFile::R16::SP);
    const auto low = cpu.read(sp);
    const auto high = cpu.read(static_cast<std::uint16_t>(sp + 1));
    specbolt::v4::delay(cpu, 1);
    cpu.write(static_cast<std::uint16_t>(sp + 1), static_cast<std::uint8_t>(value >> 8));
    cpu.write(sp, static_cast<std::uint8_t>(value));
    specbolt::v4::delay(cpu, 2);
    return static_cast<std::uint16_t>(high << 8 | low);
  }

  // `in r,(c)` addresses with the whole of bc and sets flags; `out (c),r` does
  // not. Neither is expressible as an operand, because the port space is not
  // memory.
  static Alu::R8 in_c(Cpu &cpu, const std::uint16_t port) {
    cpu.bus(Bus::io_read, port);
    const auto value = cpu.in(port);
    return {value, Alu::parity_flags_for(value)};
  }
  static void out_c(Cpu &cpu, const std::uint16_t port, const std::uint8_t value) {
    cpu.bus(Bus::io_write, port);
    cpu.out(port, value);
  }

  // `ld a,i` and `ld a,r` report iff2 in the parity flag, which is the one way
  // a program can see the interrupt state.
  static Alu::R8 ld_a_special(Cpu &cpu, const std::uint8_t value, const Flags flags) {
    return {value, Alu::iff2_flags_for(value, flags, cpu.iff2())};
  }

  // `neg` is `0 - a`, which sub8 already is.
  static Alu::R8 neg8(const std::uint8_t value) { return Alu::sub8(0, value, false); }

  // The exchanges move whole register pairs about, which no operand can name.
  static void exx(Cpu &cpu) { cpu.regs().exx(); }
  static void ex_de_hl(Cpu &cpu) { cpu.regs().ex(RegisterFile::R16::DE, RegisterFile::R16::HL); }
  static void ex_af(Cpu &cpu) { cpu.regs().ex(RegisterFile::R16::AF, RegisterFile::R16::AF_); }
};

// Where the table may name operations from.
[[nodiscard]] consteval std::array<std::meta::info, 2> primitive_scopes() { return {^^Ops, ^^Alu}; }

// Individually addressable flag bits, so `carry` is a location like any other.
enum class Bit : std::uint8_t { carry, subtract, parity, flag3, half_carry, flag5, zero, sign };

// Machine state that is not a register but is still addressable by name.
enum class State : std::uint8_t { halted, iff1, iff2 };

// A 16-bit machine register that is not in the programmer's register file.
enum class Pointer : std::uint8_t { pc };

// The high byte of the last address the machine formed -- WZ, as the Z80
// literature calls it. `bit n, (ix+d)` takes flags 3 and 5 from it.
enum class Internal : std::uint8_t { wzh };

// The interrupt vector and refresh registers, which only the ED table reaches,
// and the interrupt mode alongside them.
enum class Special : std::uint8_t { i, r, im };

// The whole flag word, distinct from R8::F so that only a Flags-shaped value
// can be written to it.
enum class Word : std::uint8_t { flags };

// Where the table may name storage locations from.
[[nodiscard]] consteval std::array<std::meta::info, 8> location_scopes() {
  return {^^RegisterFile::R8, ^^RegisterFile::R16, ^^Bit, ^^State, ^^Word, ^^Internal, ^^Pointer, ^^Special};
}

[[nodiscard]] inline std::uint8_t fetch_opcode(Cpu &cpu) { return cpu.read_opcode(); }

[[nodiscard]] inline std::uint16_t fetch_immediate(Cpu &cpu, const std::uint8_t width) {
  return width == 1 ? cpu.read_immediate() : cpu.read_immediate16();
}

// How a displacement offsets a base, and what forming that address costs. Both
// are facts about the machine: a 6502 would wrap within page zero for one mode
// and charge for a page crossing in another.
//
// The Z80 sign-extends, and spends a five-T-state window doing it -- but any
// immediate the instruction also carries is read *inside* that window, not
// before it. That is why `ld (ix+d), n` is 19 T-states and not 22, and why the
// framework says how many bytes it already read.
[[nodiscard]] inline std::uint16_t displaced_address(
    Cpu &cpu, const std::uint16_t base, const std::uint8_t offset, const std::uint8_t immediate_bytes) {
  delay(cpu, static_cast<std::uint8_t>(5 - 3 * immediate_bytes));
  return static_cast<std::uint16_t>(base + static_cast<std::int8_t>(offset));
}

[[nodiscard]] inline std::uint8_t read_memory(Cpu &cpu, const std::uint16_t address) { return cpu.read(address); }
inline void write_memory(Cpu &cpu, const std::uint16_t address, const std::uint8_t value) { cpu.write(address, value); }

// Two accesses, low byte first, because that is what the bus sees.
[[nodiscard]] inline std::uint16_t read_memory16(Cpu &cpu, const std::uint16_t address) {
  const auto low = cpu.read(address);
  return static_cast<std::uint16_t>(cpu.read(static_cast<std::uint16_t>(address + 1)) << 8 | low);
}
inline void write_memory16(Cpu &cpu, const std::uint16_t address, const std::uint16_t value) {
  cpu.write(address, static_cast<std::uint8_t>(value));
  cpu.write(static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(value >> 8));
}

[[nodiscard]] inline std::uint8_t read(const Cpu &cpu, const RegisterFile::R8 location) { return cpu.get(location); }
[[nodiscard]] inline std::uint16_t read(const Cpu &cpu, const RegisterFile::R16 location) { return cpu.get(location); }
inline void write(Cpu &cpu, const RegisterFile::R8 location, const std::uint8_t value) { cpu.set(location, value); }
inline void write(Cpu &cpu, const RegisterFile::R16 location, const std::uint16_t value) { cpu.set(location, value); }

[[nodiscard]] inline bool read(const Cpu &cpu, const Bit which) {
  return (cpu.flags().to_u8() >> static_cast<unsigned>(which) & 1u) != 0;
}
[[nodiscard]] inline Flags read(const Cpu &cpu, Word) { return cpu.flags(); }
inline void write(Cpu &cpu, Word, const Flags value) { cpu.flags(value); }
[[nodiscard]] inline bool read(const Cpu &cpu, const State which) {
  switch (which) {
    case State::iff1: return cpu.iff1();
    case State::iff2: return cpu.iff2();
    case State::halted: break;
  }
  return cpu.halted();
}
[[nodiscard]] inline std::uint8_t read(const Cpu &cpu, const Special which) {
  switch (which) {
    case Special::i: return cpu.regs().i();
    case Special::r: return cpu.regs().r();
    case Special::im: break;
  }
  return cpu.irq_mode();
}
inline void write(Cpu &cpu, const Special which, const std::uint8_t value) {
  switch (which) {
    case Special::i: cpu.regs().i(value); return;
    case Special::r: cpu.regs().r(value); return;
    case Special::im: cpu.irq_mode(value); return;
  }
}
[[nodiscard]] inline std::uint16_t read(const Cpu &cpu, Pointer) { return cpu.pc(); }
inline void write(Cpu &cpu, Pointer, const std::uint16_t value) { cpu.regs().pc(value); }
[[nodiscard]] inline std::uint8_t read(const Cpu &cpu, Internal) {
  return static_cast<std::uint8_t>(cpu.bus_address() >> 8);
}
inline void write(Cpu &cpu, const State which, const bool value) {
  switch (which) {
    case State::iff1: cpu.iff1(value); return;
    case State::iff2: cpu.iff2(value); return;
    case State::halted: cpu.halted(value); return;
  }
}

} // namespace specbolt::v4
