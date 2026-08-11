#pragma once

#ifndef SPECBOLT_MODULES
#include "Table.hpp"
#include "Z80Cpu.hpp"

#include <algorithm>
#include <array>
#include <meta>
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

[[nodiscard]] consteval std::meta::info find_primitive(const std::string_view name, const std::size_t line) {
  for (const auto scope: primitive_scopes())
    for (const auto member: std::meta::members_of(scope, std::meta::access_context::current()))
      if (std::meta::is_function(member) && std::meta::has_identifier(member) &&
          std::meta::identifier_of(member) == name)
        return member;
  throw table_error(line, "no operation '" + std::string(name) + "' in this CPU's primitives");
}

template<typename T>
[[nodiscard]] constexpr T from_word(std::type_identity<T>, const std::uint16_t value) {
  return static_cast<T>(value);
}

template<std::meta::info Fn, std::size_t I>
using parameter_type = typename[:std::meta::type_of(std::meta::parameters_of(Fn)[I]):];

// A field operand names whichever vocabulary member its slice selects. After
// that every operand is a constant, an immediate, or a name.
template<Operand Op, std::uint8_t FieldValue>
[[nodiscard]] consteval Operand resolve(const std::size_t line) {
  if constexpr (Op.kind != Operand::Kind::Field)
    return Op;
  else {
    const auto display = fields[Op.field_index].values[FieldValue].display;
    if (display.size() >= Name{}.storage.size())
      throw table_error(line, "vocabulary member name is too long to be an operand");
    return {Operand::Kind::Named, Name{display}, 0, 0, 0};
  }
}

template<Operand Op, std::uint8_t FieldValue, std::size_t Line>
[[nodiscard]] std::uint16_t value_of(const Cpu &cpu, const std::uint16_t immediate) {
  constexpr auto operand = resolve<Op, FieldValue>(Line);
  if constexpr (operand.kind == Operand::Kind::Constant)
    return operand.constant;
  else if constexpr (operand.kind == Operand::Kind::Immediate)
    return immediate;
  else
    return read(cpu, [:find_location(operand.name.view(), Line):]);
}

template<Operand Op, std::uint8_t FieldValue, std::size_t Line, typename T>
void store(Cpu &cpu, const T value) {
  constexpr auto operand = resolve<Op, FieldValue>(Line);
  static_assert(operand.kind == Operand::Kind::Named, "only a named location can be a destination");
  write(cpu, [:find_location(operand.name.view(), Line):], value);
}

// Arguments are supplied positionally; destinations destructure the result in
// declaration order. Nothing here has an opinion on what an operand means.
template<std::meta::info Fn, std::size_t Arity, std::array Operands, std::array OperandFields, std::size_t Destinations,
    std::array Targets, std::array TargetFields, std::size_t Line>
void apply(Cpu &cpu, const std::uint16_t immediate) {
  constexpr auto arguments = std::make_index_sequence<Arity>{};
  const auto call = [&]<std::size_t... I>(std::index_sequence<I...>) {
    return [:Fn:](from_word(
        std::type_identity<parameter_type<Fn, I>>{}, value_of<Operands[I], OperandFields[I], Line>(cpu, immediate))...);
  };

  using Result = decltype(call(arguments));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
  if constexpr (std::is_void_v<Result>) {
    static_assert(Destinations == 0, "this operation returns nothing, so the row may not name a destination");
    call(arguments);
  }
  else if constexpr (std::is_class_v<Result>) {
    static constexpr auto members =
        std::define_static_array(std::meta::nonstatic_data_members_of(^^Result, std::meta::access_context::current()));
    static_assert(Destinations == members.size(), "the row's destinations do not match what this operation returns");
    const auto result = call(arguments);
    template for (constexpr auto at: std::views::iota(0uz, Destinations))
        store<Targets[at], TargetFields[at], Line>(cpu, result.[:members[at]:]);
  }
  else {
    static_assert(Destinations == 1, "this operation returns one value, so the row needs exactly one destination");
    store<Targets[0], TargetFields[0], Line>(cpu, call(arguments));
  }
#pragma GCC diagnostic pop
}

// Which vocabulary member each field operand selects, for this opcode.
template<std::uint8_t Opcode>
[[nodiscard]] consteval std::array<std::uint8_t, Row::max_operands> selected(
    const Matched &matched, const std::array<Operand, Row::max_operands> &operands) {
  std::array<std::uint8_t, Row::max_operands> result{};
  for (std::size_t at = 0; at < operands.size(); ++at)
    if (operands[at].kind == Operand::Kind::Field)
      result[at] = matched.slices[operands[at].slice_index].extract(Opcode);
  return result;
}

template<std::uint8_t Opcode>
void execute_one(Cpu &cpu, const std::uint16_t immediate) {
  constexpr auto found = find_row(Opcode);
  if constexpr (found.has_value()) {
    constexpr auto row = rows[*found];
    constexpr auto member = row.verb_reference
                                ? fields[row.verb_reference->field_index]
                                      .values[row.matched.slices[row.verb_reference->slice_index].extract(Opcode)]
                                : Member{};
    constexpr auto primitive = row.verb_reference ? member.primitive : row.verb;

    // a late-bound operation may append an operand the encoding does not carry
    constexpr auto arity = row.num_operands + (member.appended ? 1u : 0u);
    constexpr auto operands = [&row = row, &member = member] {
      auto result = row.operands;
      if (member.appended)
        result[row.num_operands] = *member.appended;
      return result;
    }();

    apply<find_primitive(primitive, row.line), arity, operands, selected<Opcode>(row.matched, operands),
        row.num_destinations, row.destinations, selected<Opcode>(row.matched, row.destinations), row.line>(
        cpu, immediate);
  }
}

using Handler = void (*)(Cpu &, std::uint16_t);

inline constexpr auto dispatch = [] {
  std::array<Handler, 256> table{};
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
