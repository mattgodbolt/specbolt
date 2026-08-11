#pragma once

#ifndef SPECBOLT_MODULES
#include "Table.hpp"
#include "z80/common/RegisterFile.hpp"

#include <array>
#include <meta>
#include <string>
#endif

namespace specbolt::v4 {

struct Cpu {
  RegisterFile registers;
  bool halted{};
};

struct Ops {
  static void nop(Cpu &, unsigned, std::uint16_t) {}
  static void halt(Cpu &cpu, unsigned, std::uint16_t) { cpu.halted = true; }
  static void ld16(Cpu &cpu, const unsigned which, const std::uint16_t operand) {
    cpu.registers.set(pair_for(which), operand);
  }
  static void inc16(Cpu &cpu, const unsigned which, std::uint16_t) { add_to_pair(cpu, which, 1); }
  static void dec16(Cpu &cpu, const unsigned which, std::uint16_t) { add_to_pair(cpu, which, 0xffff); }

private:
  static RegisterFile::R16 pair_for(const unsigned which) {
    constexpr std::array pairs{
        RegisterFile::R16::BC, RegisterFile::R16::DE, RegisterFile::R16::HL, RegisterFile::R16::SP};
    return pairs[which];
  }
  static void add_to_pair(Cpu &cpu, const unsigned which, const std::uint16_t delta) {
    const auto pair = pair_for(which);
    cpu.registers.set(pair, static_cast<std::uint16_t>(cpu.registers.get(pair) + delta));
  }
};

[[nodiscard]] consteval std::meta::info find_verb(const std::string_view name, const std::size_t line) {
  for (const auto member: std::meta::members_of(^^Ops, std::meta::access_context::current()))
    if (std::meta::is_function(member) && std::meta::has_identifier(member) && std::meta::identifier_of(member) == name)
      return member;
  throw table_error(line, "no operation '" + std::string(name) + "' in Ops");
}

template<std::uint8_t Opcode>
void execute_one(Cpu &cpu, const std::uint16_t operand) {
  constexpr auto index = find_row(Opcode);
  if constexpr (index.has_value()) {
    constexpr auto row = rows[*index];
    static_assert(row.matched.num_slices <= 1, "multi-field rows are not wired up yet");
    constexpr auto which =
        row.matched.num_slices == 0 ? 0u : static_cast<unsigned>(row.matched.slices[0].extract(Opcode));
    [:find_verb(row.verb, row.line):](cpu, which, operand);
  }
}

using Handler = void (*)(Cpu &, std::uint16_t);

inline constexpr auto all_opcodes = [] {
  std::array<std::uint8_t, 256> result{};
  for (std::size_t index = 0; index < result.size(); ++index)
    result[index] = static_cast<std::uint8_t>(index);
  return result;
}();

inline constexpr auto dispatch = [] {
  std::array<Handler, 256> table{};
  // gcc reports each expansion as shadowing the previous one: PR c++/124197
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
  template for (constexpr auto opcode: all_opcodes) { table[opcode] = &execute_one<opcode>; }
#pragma GCC diagnostic pop
  return table;
}();

inline void execute(Cpu &cpu, const std::uint8_t opcode, const std::uint16_t operand = 0) {
  dispatch[opcode](cpu, operand);
}

} // namespace specbolt::v4
