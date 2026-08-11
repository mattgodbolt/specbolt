#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Alu.hpp"
#include "z80/common/RegisterFile.hpp"

#include <array>
#include <cstdint>
#include <meta>
#include <type_traits>
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

// Where the table may name storage locations from.
[[nodiscard]] consteval std::array<std::meta::info, 4> location_scopes() {
  return {^^RegisterFile::R8, ^^RegisterFile::R16, ^^Bit, ^^State};
}

[[nodiscard]] inline std::uint16_t read(const Cpu &cpu, const RegisterFile::R8 location) {
  return cpu.registers.get(location);
}
[[nodiscard]] inline std::uint16_t read(const Cpu &cpu, const RegisterFile::R16 location) {
  return cpu.registers.get(location);
}
inline void write(Cpu &cpu, const RegisterFile::R8 location, const std::uint16_t value) {
  cpu.registers.set(location, static_cast<std::uint8_t>(value));
}
inline void write(Cpu &cpu, const RegisterFile::R16 location, const std::uint16_t value) {
  cpu.registers.set(location, value);
}

[[nodiscard]] inline std::uint16_t read(const Cpu &cpu, const Bit which) {
  return (cpu.registers.get(RegisterFile::R8::F) >> static_cast<unsigned>(which)) & 1u;
}
inline void write(Cpu &cpu, const RegisterFile::R8 location, const Flags value) {
  cpu.registers.set(location, value.to_u8());
}
[[nodiscard]] inline std::uint16_t read(const Cpu &cpu, State) { return cpu.halted ? 1u : 0u; }

// How to make a parameter value out of a location's contents. The framework's
// default is a static_cast; the flag word needs the narrowing spelled out.
[[nodiscard]] constexpr Flags from_word(std::type_identity<Flags>, const std::uint16_t value) {
  return Flags(static_cast<std::uint8_t>(value));
}
inline void write(Cpu &cpu, State, const std::uint8_t value) { cpu.halted = value != 0; }

} // namespace specbolt::v4
