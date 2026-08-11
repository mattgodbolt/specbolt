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

struct Ops {
  static void nop() {}
  static std::uint16_t ld16(const std::uint16_t value) { return value; }
  static std::uint16_t inc16(const std::uint16_t value) { return static_cast<std::uint16_t>(value + 1); }
  static std::uint16_t dec16(const std::uint16_t value) { return static_cast<std::uint16_t>(value - 1); }
  static std::uint8_t ld8(const std::uint8_t value) { return value; }
  static void delay(Cpu &cpu, const std::uint8_t cycles) { cpu.pass_time(cycles); }
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
};

// Where the table may name operations from.
[[nodiscard]] consteval std::array<std::meta::info, 2> primitive_scopes() { return {^^Ops, ^^Alu}; }

// Individually addressable flag bits, so `carry` is a location like any other.
enum class Bit : std::uint8_t { carry, subtract, parity, flag3, half_carry, flag5, zero, sign };

// Machine state that is not a register but is still addressable by name.
enum class State : std::uint8_t { halted };

// Somewhere to put a value between two steps of the same instruction.
enum class Latch : std::uint8_t { t };

// The whole flag word, distinct from R8::F so that only a Flags-shaped value
// can be written to it.
enum class Word : std::uint8_t { flags };

// Where the table may name storage locations from.
[[nodiscard]] consteval std::array<std::meta::info, 6> location_scopes() {
  return {^^RegisterFile::R8, ^^RegisterFile::R16, ^^Bit, ^^State, ^^Word, ^^Latch};
}

[[nodiscard]] inline std::uint8_t fetch_opcode(Cpu &cpu) { return cpu.read_opcode(); }

[[nodiscard]] inline std::uint16_t fetch_immediate(Cpu &cpu, const std::uint8_t width) {
  return width == 1 ? cpu.read_immediate() : cpu.read_immediate16();
}

[[nodiscard]] inline std::uint8_t read_memory(Cpu &cpu, const std::uint16_t address) { return cpu.read(address); }
inline void write_memory(Cpu &cpu, const std::uint16_t address, const std::uint8_t value) { cpu.write(address, value); }

[[nodiscard]] inline std::uint8_t read(const Cpu &cpu, const RegisterFile::R8 location) { return cpu.get(location); }
[[nodiscard]] inline std::uint16_t read(const Cpu &cpu, const RegisterFile::R16 location) { return cpu.get(location); }
inline void write(Cpu &cpu, const RegisterFile::R8 location, const std::uint8_t value) { cpu.set(location, value); }
inline void write(Cpu &cpu, const RegisterFile::R16 location, const std::uint16_t value) { cpu.set(location, value); }

[[nodiscard]] inline bool read(const Cpu &cpu, const Bit which) {
  return (cpu.flags().to_u8() >> static_cast<unsigned>(which) & 1u) != 0;
}
[[nodiscard]] inline Flags read(const Cpu &cpu, Word) { return cpu.flags(); }
inline void write(Cpu &cpu, Word, const Flags value) { cpu.flags(value); }
[[nodiscard]] inline std::uint8_t read(const Cpu &cpu, Latch) { return cpu.latch(); }
inline void write(Cpu &cpu, Latch, const std::uint8_t value) { cpu.latch(value); }
[[nodiscard]] inline bool read(const Cpu &cpu, State) { return cpu.halted(); }
inline void write(Cpu &cpu, State, const bool value) { cpu.halted(value); }

} // namespace specbolt::v4
