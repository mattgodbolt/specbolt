#pragma once

#ifndef SPECBOLT_MODULES
#include "Table.hpp"
#include "Z80Cpu.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <meta>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#endif

namespace specbolt::v4 {

// The table is written the way assembler is written, so every name in it is
// matched without regard to case.
[[nodiscard]] constexpr bool same_ignoring_case(const std::string_view lhs, const std::string_view rhs) {
  constexpr auto fold = [](const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
  return std::ranges::equal(lhs, rhs, {}, fold, fold);
}

[[nodiscard]] consteval std::meta::info only_match(
    const std::span<const std::meta::info> candidates, const std::string_view name, const std::size_t line) {
  if (candidates.empty())
    throw table_error(line, "this CPU has nothing named '" + std::string(name) + "'");
  if (candidates.size() > 1)
    throw table_error(line, "this CPU has more than one thing named '" + std::string(name) + "'");
  return candidates.front();
}

[[nodiscard]] consteval std::meta::info find_location(const std::string_view name, const std::size_t line) {
  std::vector<std::meta::info> candidates;
  for (const auto scope: location_scopes())
    for (const auto enumerator: std::meta::enumerators_of(scope))
      if (same_ignoring_case(std::meta::identifier_of(enumerator), name))
        candidates.push_back(enumerator);
  return only_match(candidates, name, line);
}

[[nodiscard]] consteval std::meta::info find_primitive(const std::string_view name, const std::size_t line) {
  std::vector<std::meta::info> candidates;
  for (const auto scope: primitive_scopes())
    for (const auto member: std::meta::members_of(scope, std::meta::access_context::current()))
      if (std::meta::is_function(member) && std::meta::has_identifier(member) &&
          same_ignoring_case(std::meta::identifier_of(member), name))
        candidates.push_back(member);
  return only_match(candidates, name, line);
}

template<std::meta::info Fn>
inline constexpr std::size_t arity_of = std::meta::parameters_of(Fn).size();

template<std::meta::info Fn, std::size_t I>
using parameter_type = typename[:std::meta::type_of(std::meta::parameters_of(Fn)[I]):];

// A primitive may ask for the machine itself, which is the one type the
// framework is parameterised on and so the one it can always supply.
template<std::meta::info Fn>
[[nodiscard]] consteval bool takes_cpu() {
  if constexpr (arity_of<Fn> == 0)
    return false;
  else
    return std::is_same_v<parameter_type<Fn, 0>, Cpu &>;
}

// Everything one step needs, with its field references already resolved.
struct Call {
  std::array<Operand, max_operands> operands{};
  std::size_t num_operands{};
  std::array<Operand, max_operands> destinations{};
  std::size_t num_destinations{};
  std::size_t line{};
};

// Only a literal written in the table is narrowed on the author's say-so;
// everything else converts implicitly, so handing a 16-bit location to an
// 8-bit parameter is a diagnosable narrowing rather than a silent truncation.
template<Operand Op, std::size_t Line, typename Parameter>
[[nodiscard]] Parameter direct_value_of(Cpu &cpu, const std::uint16_t immediate) {
  if constexpr (Op.kind == Operand::Kind::Constant)
    return static_cast<Parameter>(Op.constant);
  else if constexpr (Op.kind == Operand::Kind::Immediate) {
    if constexpr (Op.width == 1)
      return static_cast<std::uint8_t>(immediate);
    else
      return immediate;
  }
  else
    return read(cpu, [:find_location(Op.name.view(), Line):]);
}

// An indirect operand is whatever it would have been, read as an address.
template<Operand Op, std::size_t Line, typename Parameter>
[[nodiscard]] Parameter value_of(Cpu &cpu, const std::uint16_t immediate) {
  if constexpr (Op.indirect)
    return read_memory(cpu, direct_value_of<Op, Line, std::uint16_t>(cpu, immediate));
  else
    return direct_value_of<Op, Line, Parameter>(cpu, immediate);
}

template<Operand Op, std::size_t Line, typename T>
void store(Cpu &cpu, const std::uint16_t immediate, const T value) {
  if constexpr (Op.kind == Operand::Kind::Discard)
    static_cast<void>(value);
  else if constexpr (Op.indirect)
    write_memory(cpu, direct_value_of<Op, Line, std::uint16_t>(cpu, immediate), value);
  else {
    static_assert(Op.kind == Operand::Kind::Named, "only a named location can be a destination");
    write(cpu, [:find_location(Op.name.view(), Line):], value);
  }
}

// Arguments are supplied positionally; destinations destructure the result in
// declaration order. Nothing here has an opinion on what an operand means.
template<std::meta::info Fn, Call C>
void apply(Cpu &cpu, const std::uint16_t immediate) {
  constexpr std::size_t supplied = takes_cpu<Fn>() ? 1 : 0;
  static_assert(
      C.num_operands + supplied == arity_of<Fn>, "the row supplies the wrong number of operands for this operation");
  constexpr auto arguments = std::make_index_sequence<C.num_operands>{};
  const auto call = [&]<std::size_t... I>(std::index_sequence<I...>) {
    if constexpr (takes_cpu<Fn>())
      return [:Fn:](cpu, value_of<C.operands[I], C.line, parameter_type<Fn, I + 1>>(cpu, immediate)...);
    else
      return [:Fn:](value_of<C.operands[I], C.line, parameter_type<Fn, I>>(cpu, immediate)...);
  };

  using Result = decltype(call(arguments));
  if constexpr (std::is_void_v<Result>) {
    static_assert(C.num_destinations == 0, "this operation returns nothing, so the row may not name a destination");
    call(arguments);
  }
  else if constexpr (C.num_destinations == 1) {
    store<C.destinations[0], C.line>(cpu, immediate, call(arguments));
  }
  else {
    static constexpr auto members =
        std::define_static_array(std::meta::nonstatic_data_members_of(^^Result, std::meta::access_context::current()));
    static_assert(
        C.num_destinations == members.size(), "the row's destinations do not match what this operation returns");
    const auto result = call(arguments);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow" // PR c++/124197: `template for` sees its own induction variable
    template for (constexpr auto at: std::views::iota(0uz, C.num_destinations))
        store<C.destinations[at], C.line>(cpu, immediate, result.[:members[at]:]);
#pragma GCC diagnostic pop
  }
}

// A vocabulary member may bind the verb late, and may append an operand the
// encoding does not carry.
[[nodiscard]] consteval Member member_for(const Step &step, const Matched &matched, const std::uint8_t opcode) {
  if (!step.verb_reference)
    return {};
  return fields[step.verb_reference->field_index]
      .values[matched.slices[step.verb_reference->slice_index].extract(opcode)];
}

[[nodiscard]] consteval Call call_for(
    const Step &step, const Matched &matched, const std::uint8_t opcode, const std::size_t line) {
  const auto member = member_for(step, matched, opcode);
  Call result{{}, step.num_operands, {}, step.num_destinations, line};
  for (std::size_t at = 0; at < step.num_operands; ++at)
    result.operands[at] = resolve(step.operands[at], matched, opcode, line);
  for (std::size_t at = 0; at < step.num_destinations; ++at)
    result.destinations[at] = resolve(step.destinations[at], matched, opcode, line);
  if (member.appended)
    result.operands[result.num_operands++] = *member.appended;
  return result;
}

[[nodiscard]] consteval std::uint8_t immediate_width_of(const Row &row, const std::uint8_t opcode) {
  std::uint8_t width = 0;
  for (std::size_t at = 0; at < row.num_steps; ++at) {
    const auto call = call_for(row.steps[at], row.matched, opcode, row.line);
    for (std::size_t operand = 0; operand < call.num_operands; ++operand)
      if (call.operands[operand].kind == Operand::Kind::Immediate)
        width = call.operands[operand].width;
  }
  return width;
}

template<std::uint8_t Opcode, std::size_t Index>
void execute_one(Cpu &cpu) {
  constexpr auto row = rows[Index];
  // Fetched once, before any step, because argument order within a call is
  // unspecified and a later step may store through an earlier one's address.
  constexpr auto width = immediate_width_of(row, Opcode);
  const std::uint16_t immediate = width == 0 ? 0 : fetch_immediate(cpu, width);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow" // PR c++/124197: `template for` sees its own induction variable
  template for (constexpr auto at: std::views::iota(0uz, row.num_steps)) {
    constexpr auto step = row.steps[at];
    constexpr auto member = member_for(step, row.matched, Opcode);
    constexpr auto primitive = step.verb_reference ? member.primitive : step.verb;
    apply<find_primitive(primitive, row.line), call_for(step, row.matched, Opcode, row.line)>(cpu, immediate);
  }
#pragma GCC diagnostic pop
}

using Handler = void (*)(Cpu &);

template<std::uint8_t Opcode>
inline constexpr Handler handler_for = [] {
  constexpr auto found = find_row(Opcode);
  if constexpr (found)
    return &execute_one<Opcode, *found>;
  else
    return nullptr;
}();

inline constexpr auto dispatch = [] {
  std::array<Handler, 256> table{};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow" // PR c++/124197: `template for` sees its own induction variable
  template for (constexpr auto opcode: std::views::iota(0uz, 256uz)) table[opcode] =
      handler_for<static_cast<std::uint8_t>(opcode)>;
#pragma GCC diagnostic pop
  return table;
}();

inline void execute(Cpu &cpu, const std::uint8_t opcode) {
  const auto handler = dispatch[opcode];
  if (!handler)
    throw std::runtime_error(std::format("no row in " SPECBOLT_CPU_TABLE " decodes opcode 0x{:02x}", opcode));
  handler(cpu);
}

} // namespace specbolt::v4
