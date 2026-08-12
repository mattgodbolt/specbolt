#pragma once

#ifndef SPECBOLT_MODULES
#include "Table.hpp"
#include "Z80Cpu.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <format>
#include <limits>
#include <meta>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
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
      // `has_identifier` excludes the implicitly-declared special members, which
      // have no name to compare. `is_static_member` excludes ordinary member
      // functions, which cannot be called without an object.
      if (std::meta::is_function(member) && std::meta::is_static_member(member) && std::meta::has_identifier(member) &&
          same_ignoring_case(std::meta::identifier_of(member), name))
        candidates.push_back(member);
  return only_match(candidates, name, line);
}

template<std::meta::info Fn>
inline constexpr std::size_t arity_of = std::meta::parameters_of(Fn).size();

template<std::meta::info Fn, std::size_t I>
using parameter_type = typename[:std::meta::type_of(std::meta::parameters_of(Fn)[I]):];

// What a result destructures into. A class with nothing *accessible* -- `Flags`
// keeps its byte private -- is one value, not none: `access_context::current()`
// is this namespace, so a private member is not visible here and is not counted.
[[nodiscard]] consteval std::span<const std::meta::info> destructures_into(const std::meta::info type) {
  if (!std::meta::is_class_type(type))
    return {};
  return std::define_static_array(std::meta::nonstatic_data_members_of(type, std::meta::access_context::current()));
}

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
  Vector<Operand, max_operands> operands{};
  Vector<Operand, max_operands> destinations{};
  std::size_t line{};
};

// Only a literal written in the table is narrowed on the author's say-so;
// everything else converts implicitly, so handing a 16-bit location to an
// 8-bit parameter is a diagnosable narrowing rather than a silent truncation.
template<Operand Op, std::size_t Line, typename Parameter>
[[nodiscard]] Parameter direct_value_of(Cpu &cpu, const std::uint16_t immediate) {
  static_assert(!std::is_reference_v<Parameter>,
      "a primitive takes its operands by value; there is nothing here for a reference to bind to");
  if constexpr (Op.kind == Operand::Kind::Constant) {
    if constexpr (std::integral<Parameter>)
      static_assert(Op.constant <= static_cast<std::uintmax_t>(std::numeric_limits<Parameter>::max()),
          "this constant does not fit the parameter it is passed to");
    return static_cast<Parameter>(Op.constant);
  }
  else if constexpr (Op.kind == Operand::Kind::Immediate) {
    if constexpr (Op.width == 1)
      return static_cast<std::uint8_t>(immediate);
    else
      return immediate;
  }
  else
    return read(cpu, [:find_location(Op.name.view(), Line):]);
}

// The address an indirect operand addresses through. A displaced one was formed
// once for the whole instruction, before any operand was touched.
template<Operand Op, std::size_t Line>
[[nodiscard]] std::uint16_t address_of(Cpu &cpu, const std::uint16_t immediate, const std::uint16_t indexed) {
  if constexpr (Op.displaced)
    return indexed;
  else
    return direct_value_of<Op, Line, std::uint16_t>(cpu, immediate);
}

// An indirect operand is whatever it would have been, read as an address. How
// wide the read is comes from the parameter it feeds, so `ld16 hl <- (n)` reads
// two bytes and `ld8 a <- (n)` one, with the row saying neither.
template<Operand Op, std::size_t Line, typename Parameter>
[[nodiscard]] Parameter value_of(Cpu &cpu, const std::uint16_t immediate, const std::uint16_t indexed) {
  if constexpr (Op.indirect) {
    const auto address = address_of<Op, Line>(cpu, immediate, indexed);
    if constexpr (std::same_as<Parameter, std::uint16_t>)
      return read_memory16(cpu, address);
    else
      return read_memory(cpu, address);
  }
  else
    return direct_value_of<Op, Line, Parameter>(cpu, immediate);
}

template<Operand Op, std::size_t Line, typename T>
void store(Cpu &cpu, const std::uint16_t immediate, const std::uint16_t indexed, const T value) {
  if constexpr (Op.kind == Operand::Kind::Discard)
    static_cast<void>(value);
  else if constexpr (Op.indirect) {
    // The addressing mode says how long the machine idles before writing back.
    if constexpr (Op.write_back_delay != 0)
      delay(cpu, Op.write_back_delay);
    const auto address = address_of<Op, Line>(cpu, immediate, indexed);
    if constexpr (std::same_as<T, std::uint16_t>)
      write_memory16(cpu, address, value);
    else
      write_memory(cpu, address, value);
  }
  else {
    static_assert(Op.kind == Operand::Kind::Named, "only a named location can be a destination");
    write(cpu, [:find_location(Op.name.view(), Line):], value);
  }
}

// Resolving an operand is not a pure act: it can read memory, advance the clock
// and move the address bus. So the order matters, and the order a function's
// arguments are evaluated in is *unspecified* -- gcc evaluates them right to
// left. Braced initialisation is sequenced left to right ([dcl.init.list]/4),
// so the values are materialised into a tuple first and the call made from
// that.
//
// `bit n, (ix+d)` is the row that proves this is not pedantry: it reads memory
// and then asks for the address that read left on the bus, and getting those
// two the wrong way round takes the undocumented flags from the wrong place.
template<std::meta::info Fn, Call C>
[[nodiscard]] auto operands_of(Cpu &cpu, const std::uint16_t immediate, const std::uint16_t indexed) {
  constexpr std::size_t supplied = takes_cpu<Fn>() ? 1 : 0;
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return std::tuple{value_of<C.operands[I], C.line, parameter_type<Fn, I + supplied>>(cpu, immediate, indexed)...};
  }(std::make_index_sequence<C.operands.size()>{});
}

// Arguments are supplied positionally; destinations destructure the result in
// declaration order. Nothing here has an opinion on what an operand means.
template<std::meta::info Fn, Call C>
void apply(Cpu &cpu, const std::uint16_t immediate, const std::uint16_t indexed) {
  constexpr std::size_t supplied = takes_cpu<Fn>() ? 1 : 0;
  static_assert(
      C.operands.size() + supplied == arity_of<Fn>, "the row supplies the wrong number of operands for this operation");
  const auto call = [&](const auto &arguments) {
    return std::apply(
        [&cpu](const auto &...values) {
          if constexpr (takes_cpu<Fn>())
            return [:Fn:](cpu, values...);
          else
            return [:Fn:](values...);
        },
        arguments);
  };

  using Result = decltype(call(operands_of<Fn, C>(cpu, immediate, indexed)));
  static constexpr auto members = destructures_into(^^Result);
  static_assert(members.size() != 1,
      "a result with exactly one accessible member is ambiguous: it is neither a value nor a pair");
  if constexpr (std::is_void_v<Result>) {
    static_assert(C.destinations.size() == 0, "this operation returns nothing, so the row may not name a destination");
    call(operands_of<Fn, C>(cpu, immediate, indexed));
  }
  else if constexpr (members.size() > 1) {
    // Two is `Alu`'s `{result, flags}`, which is what almost every arithmetic
    // primitive returns. One accessible member is caught above as ambiguous.
    static_assert(
        C.destinations.size() == members.size(), "the row's destinations do not match what this operation returns");
    const auto result = call(operands_of<Fn, C>(cpu, immediate, indexed));
    template for (constexpr auto at: std::views::iota(0uz, C.destinations.size()))
        store<C.destinations[at], C.line>(cpu, immediate, indexed, result.[:members[at]:]);
  }
  else {
    static_assert(C.destinations.size() >= 1, "this operation returns a value, so the row must name a destination");
    // More than one is how `dd cb d op` writes its result back through the
    // addressing mode *and* into the register the low bits name.
    const auto result = call(operands_of<Fn, C>(cpu, immediate, indexed));
    template for (constexpr auto at: std::views::iota(0uz, C.destinations.size()))
        store<C.destinations[at], C.line>(cpu, immediate, indexed, result);
  }
}

// A condition is applied like any other primitive; only what is done with the
// answer differs.
template<std::meta::info Fn, Call C>
[[nodiscard]] bool evaluate(Cpu &cpu, const std::uint16_t immediate, const std::uint16_t indexed) {
  static_assert(C.operands.size() == arity_of<Fn>, "the row supplies the wrong number of operands for this condition");
  static_assert(C.destinations.size() == 0, "a condition names no destination; it decides whether the rest happens");
  static_assert(!takes_cpu<Fn>(), "a condition may not ask for the machine; it only reads what the row hands it");
  static_assert(std::is_same_v<typename[:std::meta::return_type_of(Fn):], bool>, "a condition must answer yes or no");
  return std::apply(
      [](const auto &...values) { return [:Fn:](values...); }, operands_of<Fn, C>(cpu, immediate, indexed));
}

// A vocabulary member may bind the verb late, and may append an operand the
// encoding does not carry.
[[nodiscard]] consteval Member member_for(
    const Step &step, const Matched &matched, const std::uint8_t opcode, const Rules &rules) {
  if (!step.verb_reference)
    return {};
  return member_of(fields, *step.verb_reference, matched, opcode, rules);
}

[[nodiscard]] consteval Call call_for(
    const Step &step, const Matched &matched, const std::uint8_t opcode, const std::size_t line, const Rules &rules) {
  const auto member = member_for(step, matched, opcode, rules);
  Call result{.line = line};
  for (const auto &operand: step.operands)
    result.operands.push_back(resolve(fields, operand, matched, opcode, rules), line, "too many operands");
  for (const auto &target: step.destinations) {
    auto destination = resolve(fields, target, matched, opcode, rules);
    // The idle cycle belongs to a write-back, so only to something also read.
    const auto was_read = std::ranges::any_of(
        result.operands, [&](const Operand &operand) { return operand.indirect && operand.name == destination.name; });
    if (!was_read)
      destination.write_back_delay = 0;
    result.destinations.push_back(destination, line, "too many destinations");
  }
  // A vocabulary member may append an operand the encoding does not carry.
  if (member.appended)
    result.operands.push_back(*member.appended, line, "too many operands");
  return result;
}

// What a row says to do once it has run: nothing, or fetch another byte and
// decode it in the table named. A prefix *returns* where to go rather than
// going there, because `dd dd dd ...` is a legal and unbounded Z80 instruction:
// it must cost 4T a byte, not a stack frame a byte. The displacement rides
// along because `dd cb d op` reads its displacement one table before the row
// that uses it.
struct Transfer {
  std::uint8_t table{};
  std::uint8_t displacement{};
};
using Next = std::optional<Transfer>;
using Handler = Next (*)(Cpu &, std::uint8_t);

template<std::uint8_t Table, std::uint8_t Opcode, std::size_t Index>
Next execute_one(Cpu &cpu, const std::uint8_t latch) {
  constexpr auto row = rows[Index];
  // A renaming applies to every row this table decodes, inherited or its own: a
  // rule names the vocabulary it rewrites, so `ld {s:y}, (ix+d)` keeps the real
  // h by naming a vocabulary no rule mentions.
  constexpr auto rules = tables[Table].rules;
  // The displacement is read before any immediate, which is the order the bytes
  // appear in: `dd 36 d n` is `ld (ix+d), n`.
  // Unless this table was entered with a displacement already read, in which
  // case it arrived before this row's own opcode did.
  constexpr auto displaced = displaced_through(fields, row, Opcode, rules);
  constexpr bool inherits = latched[Table];
  const std::uint8_t displacement =
      row.reads_displacement || (displaced && !inherits) ? static_cast<std::uint8_t>(fetch_immediate(cpu, 1)) : latch;
  // The encoding column says what is fetched, and it is fetched once before any
  // step: argument order within a call is unspecified, and a later step may
  // store through an address an earlier one read.
  const std::uint16_t immediate = row.immediate_bytes == 0 ? 0 : fetch_immediate(cpu, row.immediate_bytes);
  // Formed once, after both, and handed to every operand that shares it. The
  // machine is told what else was read first, because on a Z80 those reads
  // happen *inside* the window that forms the address rather than before it.
  const std::uint16_t indexed = [&] -> std::uint16_t {
    if constexpr (displaced) {
      // A table entered latched read its opcode inside the same window, so that
      // byte counts too: it is why `dd cb d op` spends five cycles and not eight.
      //
      // The machine is told the count and works out what is left of the window
      // from it, so a count the window cannot hold asks it for a negative delay.
      // Caught here, where the number is a constant, rather than at run time as
      // an enormous unsigned one.
      constexpr auto read_inside = row.immediate_bytes + (inherits ? 1 : 0);
      static_assert(
          read_inside <= 1, "this row reads more inside the window that forms its address than the window can hold");
      return displaced_address(cpu, direct_value_of<*displaced, row.line, std::uint16_t>(cpu, immediate), displacement,
          static_cast<std::uint8_t>(read_inside));
    }
    else
      return 0;
  }();
  template for (constexpr auto at: std::views::iota(0uz, row.steps.size())) {
    constexpr auto step = row.steps[at];
    if constexpr (step.kind == Step::Kind::Goto)
      return Transfer{step.target, displacement};
    else {
      constexpr auto member = member_for(step, row.matched, Opcode, rules);
      constexpr auto primitive = step.verb_reference ? member.primitive : step.verb;
      constexpr auto call = call_for(step, row.matched, Opcode, row.line, rules);
      if constexpr (step.kind == Step::Kind::If) {
        // The rest of the row is the conditional half, which is where the
        // extra cycles of a taken branch come from too.
        if (!evaluate<find_primitive(primitive, row.line), call>(cpu, immediate, indexed))
          return std::nullopt;
      }
      else
        apply<find_primitive(primitive, row.line), call>(cpu, immediate, indexed);
    }
  }
  return std::nullopt;
}

template<std::uint8_t Table, std::uint8_t Opcode>
inline constexpr Handler handler_for = [] {
  constexpr auto found = find_row(Table, Opcode);
  if constexpr (found)
    return &execute_one<Table, Opcode, *found>;
  else
    return nullptr;
}();

template<std::uint8_t Table>
inline constexpr auto dispatch = [] {
  std::array<Handler, 256> handlers{};
  template for (constexpr auto opcode: std::views::iota(0uz, 256uz)) handlers[opcode] =
      handler_for<Table, static_cast<std::uint8_t>(opcode)>;
  return handlers;
}();

// The loop's table is not a constant after the first byte, so every table's
// dispatch has to be reachable by index. A pack rather than a `template for`:
// gcc 16.2 still reports an expansion variable in a non-dependent context as
// shadowing itself (PR c++/124197).
template<std::size_t... Table>
[[nodiscard]] consteval auto all_dispatches(std::index_sequence<Table...>) {
  return std::array{dispatch<static_cast<std::uint8_t>(Table)>...};
}

inline constexpr auto dispatches = all_dispatches(std::make_index_sequence<tables.size()>{});

// Fetch, decode, run; and go round again while what ran was a prefix. Each turn
// of the loop is a real opcode fetch, so the loop always advances time and
// always advances PC -- which is why a table may now reach itself.
inline void execute_instruction(Cpu &cpu) {
  auto table = entry_table;
  std::uint8_t latch = 0;
  while (true) {
    // A latched table's opcode is not the instruction's first unknown byte, so
    // it arrives as an operand read: three cycles, and no refresh.
    const auto opcode = latched[table] ? static_cast<std::uint8_t>(fetch_immediate(cpu, 1)) : fetch_opcode(cpu);
    const auto handler = dispatches[table][opcode];
    if (!handler)
      throw std::runtime_error(std::format(
          "no row in " SPECBOLT_CPU_TABLE " table '{}' decodes opcode 0x{:02x}", tables[table].name, opcode));
    const auto next = handler(cpu, latch);
    if (!next)
      return;
    table = next->table;
    latch = next->displacement;
  }
}

} // namespace specbolt::v4
