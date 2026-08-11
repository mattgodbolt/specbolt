#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Alu.hpp"
#include "z80/common/RegisterFile.hpp"

#include <array>
#include <cstdint>
#include <meta>
#endif

// Everything the framework needs to know about the Z80, and nothing it needs to
// know about any other CPU. Retargeting means writing one of these.

namespace specbolt::v4 {

struct Cpu {
  RegisterFile registers;
  bool halted{};
};

struct Ops {
  static void nop() {}
  static std::uint16_t ld16(const std::uint16_t value) { return value; }
  static std::uint16_t inc16(const std::uint16_t value) { return static_cast<std::uint16_t>(value + 1); }
  static std::uint16_t dec16(const std::uint16_t value) { return static_cast<std::uint16_t>(value - 1); }
  static std::uint8_t ld8(const std::uint8_t value) { return value; }
};

// Where the table may name operations from.
[[nodiscard]] consteval std::array<std::meta::info, 2> primitive_scopes() { return {^^Ops, ^^Alu}; }

// Individually addressable flag bits, so `carry` is a location like any other.
enum class Bit : std::uint8_t { carry, subtract, parity, flag3, half_carry, flag5, zero, sign };

// Machine state that is not a register but is still addressable by name.
enum class State : std::uint8_t { halted };

// The whole flag word, distinct from R8::F so that only a Flags-shaped value
// can be written to it.
enum class Word : std::uint8_t { flags };

// Where the table may name storage locations from.
[[nodiscard]] consteval std::array<std::meta::info, 5> location_scopes() {
  return {^^RegisterFile::R8, ^^RegisterFile::R16, ^^Bit, ^^State, ^^Word};
}

[[nodiscard]] inline std::uint8_t read(const Cpu &cpu, const RegisterFile::R8 location) {
  return cpu.registers.get(location);
}
[[nodiscard]] inline std::uint16_t read(const Cpu &cpu, const RegisterFile::R16 location) {
  return cpu.registers.get(location);
}
inline void write(Cpu &cpu, const RegisterFile::R8 location, const std::uint8_t value) {
  cpu.registers.set(location, value);
}
inline void write(Cpu &cpu, const RegisterFile::R16 location, const std::uint16_t value) {
  cpu.registers.set(location, value);
}

[[nodiscard]] inline bool read(const Cpu &cpu, const Bit which) {
  return ((cpu.registers.get(RegisterFile::R8::F) >> static_cast<unsigned>(which)) & 1u) != 0;
}
[[nodiscard]] inline Flags read(const Cpu &cpu, Word) { return Flags(cpu.registers.get(RegisterFile::R8::F)); }
inline void write(Cpu &cpu, Word, const Flags value) { cpu.registers.set(RegisterFile::R8::F, value.to_u8()); }
[[nodiscard]] inline bool read(const Cpu &cpu, State) { return cpu.halted; }
inline void write(Cpu &cpu, State, const bool value) { cpu.halted = value; }

} // namespace specbolt::v4
