#pragma once

// What a row's names mean: everything a description of this chip may read or
// write, and how each kind is reached.
//
// The registers come from `RegisterFile`; the rest are enums declared here so
// that a flag bit, the halted state or the program counter can be named in a
// row exactly as `a` or `hl` is. A splice of one of these picks the `read` or
// `write` below by ordinary overload resolution -- which is why the framework
// needs no idea what kind of location it is holding.

#include "z80/common/Alu.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/v4/Operations.hpp"

#include <array>
#include <cstdint>
#include <meta>

namespace specbolt::v4 {

// Individually addressable flag bits, so `carry` is a location like any other.
enum class Bit : std::uint8_t { carry, subtract, parity, flag3, half_carry, flag5, zero, sign };

// Machine state that is not a register but is still addressable by name.
enum class State : std::uint8_t { halted, iff1, iff2, deferred };

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
    case State::deferred: return cpu.interrupts_deferred();
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
    case State::deferred: cpu.interrupts_deferred(value); return;
    case State::halted: cpu.halted(value); return;
  }
}

} // namespace specbolt::v4
