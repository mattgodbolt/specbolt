#pragma once

#ifndef SPECBOLT_MODULES
#include "Table.hpp"
#include "Z80Cpu.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <meta>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#endif

namespace specbolt::v4 {

[[nodiscard]] constexpr bool same_ignoring_case(const std::string_view lhs, const std::string_view rhs) {
  constexpr auto fold = [](const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
  return std::ranges::equal(lhs, rhs, {}, fold, fold);
}

[[nodiscard]] consteval std::meta::info find_location(const std::string_view name, const std::size_t line) {
  for (const auto scope: location_scopes())
    for (const auto enumerator: std::meta::enumerators_of(scope))
      if (same_ignoring_case(std::meta::identifier_of(enumerator), name))
        return enumerator;
  throw table_error(line, "no location named '" + std::string(name) + "' in this CPU");
}

// The scopes this CPU's table may name operations from. Per-CPU configuration,
// not framework knowledge: a 6502 would list its own.
[[nodiscard]] consteval std::meta::info find_primitive(const std::string_view name, const std::size_t line) {
  for (const auto scope: primitive_scopes())
    for (const auto member: std::meta::members_of(scope, std::meta::access_context::current()))
      if (std::meta::is_function(member) && std::meta::has_identifier(member) &&
          std::meta::identifier_of(member) == name)
        return member;
  throw table_error(line, "no operation '" + std::string(name) + "' in this CPU's primitives");
}

[[nodiscard]] consteval std::size_t operand_index_of(const std::meta::info fn, const std::size_t upto) {
  std::size_t count = 0;
  const auto parameters = std::meta::parameters_of(fn);
  for (std::size_t index = 0; index < upto; ++index)
    if (!is_supplied_by_framework(parameters[index]))
      ++count;
  return count;
}

template<std::meta::info Fn>
inline constexpr std::size_t arity_of = std::meta::parameters_of(Fn).size();

template<std::meta::info Fn, std::size_t I>
using parameter_type = typename[:std::meta::type_of(std::meta::parameters_of(Fn)[I]):];

template<typename T, std::size_t OperandIndex, CarrySource Carry>
[[nodiscard]] constexpr T argument_for(
    Cpu &cpu, const CpuFlags flags, const std::array<std::uint16_t, Row::max_operands> &operands) {
  if constexpr (std::same_as<T, Cpu &>)
    return cpu;
  else if constexpr (std::same_as<T, CpuFlags>)
    return flags;
  else if constexpr (std::same_as<T, bool>)
    return Carry == CarrySource::FromFlags && flags.carry();
  else
    return static_cast<T>(operands[OperandIndex]);
}

template<Operand Op, std::uint8_t FieldValue, std::size_t Line>
[[nodiscard]] std::uint16_t operand_value(const Cpu &cpu, const std::uint16_t immediate) {
  if constexpr (Op.kind == Operand::Kind::Accumulator)
    return read(cpu, accumulator);
  else if constexpr (Op.kind == Operand::Kind::Immediate)
    return immediate;
  else
    return read(cpu, [:find_location(fields[Op.field_index].values[FieldValue].display, Line):]);
}

template<Operand Op, std::uint8_t FieldValue, std::size_t Line>
void store(Cpu &cpu, const std::uint16_t value) {
  if constexpr (Op.kind == Operand::Kind::Accumulator)
    write(cpu, accumulator, value);
  else if constexpr (Op.kind == Operand::Kind::Field)
    write(cpu, [:find_location(fields[Op.field_index].values[FieldValue].display, Line):], value);
  else
    static_assert(false, "an immediate cannot be a destination");
}

template<typename T>
concept ValueAndFlags = requires(T result) {
  result.result;
  { result.flags } -> std::convertible_to<CpuFlags>;
};

template<typename Write, std::meta::info Fn, CarrySource Carry, bool HasDestination>
void apply(Cpu &cpu, const std::array<std::uint16_t, Row::max_operands> &operands, Write write) {
  const auto flags = flags_of(cpu);
  const auto call = [&]<std::size_t... I>(std::index_sequence<I...>) {
    return [:Fn:](argument_for<parameter_type<Fn, I>, operand_index_of(Fn, I), Carry>(cpu, flags, operands)...);
  };
  constexpr auto parameters = std::make_index_sequence<arity_of<Fn>>{};
  using Result = decltype(call(parameters));
  if constexpr (std::is_void_v<Result>) {
    call(parameters);
  }
  else if constexpr (std::same_as<Result, CpuFlags>) {
    set_flags(cpu, call(parameters));
  }
  else {
    static_assert(HasDestination, "this action returns a value but the row declares no destination");
    const auto result = call(parameters);
    if constexpr (ValueAndFlags<Result>) {
      write(static_cast<std::uint16_t>(result.result));
      set_flags(cpu, result.flags);
    }
    else {
      write(static_cast<std::uint16_t>(result));
    }
  }
}

template<std::uint8_t Opcode>
void execute_one(Cpu &cpu, const std::uint16_t immediate) {
  constexpr auto index = find_row(Opcode);
  if constexpr (index.has_value()) {
    constexpr auto row = rows[*index];
    std::array<std::uint16_t, Row::max_operands> operands{};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto at: std::views::iota(0uz, row.num_operands)) {
      constexpr auto operand = row.operands[at];
      constexpr auto value = operand.kind == Operand::Kind::Field
                                 ? row.matched.slices[operand.slice_index].extract(Opcode)
                                 : std::uint8_t{0};
      operands[at] = operand_value<operand, value, row.line>(cpu, immediate);
    }
#pragma GCC diagnostic pop
    const auto write = [&](const std::uint16_t value) {
      if constexpr (row.destination) {
        constexpr auto destination = *row.destination;
        constexpr auto field_value = destination.kind == Operand::Kind::Field
                                         ? row.matched.slices[destination.slice_index].extract(Opcode)
                                         : std::uint8_t{0};
        store<destination, field_value, row.line>(cpu, value);
      }
    };
    if constexpr (row.verb_reference) {
      constexpr auto reference = *row.verb_reference;
      constexpr auto member =
          fields[reference.field_index].values[row.matched.slices[reference.slice_index].extract(Opcode)];
      apply<decltype(write), find_primitive(member.primitive, row.line), member.carry, row.destination.has_value()>(
          cpu, operands, write);
    }
    else {
      apply<decltype(write), find_primitive(row.verb, row.line), CarrySource::Zero, row.destination.has_value()>(
          cpu, operands, write);
    }
  }
}

using Handler = void (*)(Cpu &, std::uint16_t);

inline constexpr auto dispatch = [] {
  std::array<Handler, 256> table{};
  // gcc reports each expansion as shadowing the previous one: PR c++/124197
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
  template for (constexpr auto opcode: std::views::iota(0uz, 256uz)) table[opcode] =
      &execute_one<static_cast<std::uint8_t>(opcode)>;
#pragma GCC diagnostic pop
  return table;
}();

inline void execute(Cpu &cpu, const std::uint8_t opcode, const std::uint16_t immediate = 0) {
  dispatch[opcode](cpu, immediate);
}

} // namespace specbolt::v4
