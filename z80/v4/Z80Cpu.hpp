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
  static void halt(Cpu &cpu) { cpu.halted = true; }
  static std::uint16_t ld16(const std::uint16_t value) { return value; }
  static std::uint16_t inc16(const std::uint16_t value) { return static_cast<std::uint16_t>(value + 1); }
  static std::uint16_t dec16(const std::uint16_t value) { return static_cast<std::uint16_t>(value - 1); }
  static std::uint8_t ld8(const std::uint8_t value) { return value; }
};

// Where the table may name operations from.
[[nodiscard]] consteval std::array<std::meta::info, 2> primitive_scopes() { return {^^Ops, ^^Alu}; }

// Where it may name storage locations from, and which one is the accumulator.
[[nodiscard]] consteval std::array<std::meta::info, 2> location_scopes() {
  return {^^RegisterFile::R8, ^^RegisterFile::R16};
}
inline constexpr auto accumulator = RegisterFile::R8::A;

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

// The CPU's flag word, and where it lives.
using CpuFlags = Flags;
[[nodiscard]] inline CpuFlags flags_of(const Cpu &cpu) { return CpuFlags(cpu.registers.get(RegisterFile::R8::F)); }
inline void set_flags(Cpu &cpu, const CpuFlags flags) { cpu.registers.set(RegisterFile::R8::F, flags.to_u8()); }

// The framework supplies these itself rather than taking them from the row.
[[nodiscard]] consteval bool is_supplied_by_framework(const std::meta::info parameter) {
  const auto type = std::meta::type_of(parameter);
  return type == ^^Flags || type == ^^bool || type == ^^Cpu &;
}

} // namespace specbolt::v4
