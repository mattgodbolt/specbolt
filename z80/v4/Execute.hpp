#pragma once

#ifndef SPECBOLT_MODULES
#include "Table.hpp"
#include "Z80Cpu.hpp"

#include <algorithm>
#include <array>
#include <concepts>
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

// This file turns a parsed instruction table into an interpreter. `Table.hpp`
// has already read `z80.cpu` and lowered it to `constexpr` data; nothing here
// parses anything. What is left is to look up the names that data holds in the
// CPU description, and to emit one function per (table, opcode).
//
// Reading order, roughly top to bottom:
//
//   find_location / find_primitive   a name in the table -> an entity in C++
//   direct_value_of / value_of       an operand -> a value, reading if it must
//   store                            a value -> an operand, writing if it must
//   operands_of / apply / evaluate   one step
//   execute_one                      one row: every step, unrolled
//   dispatch / execute_instruction   256 of those per table, and the loop
//
// ---------------------------------------------------------------------------
// The C++26 features, and what each is doing here
// ---------------------------------------------------------------------------
//
// **Reflection (P2996).** `^^X` yields a `std::meta::info`: one type that can
// denote *any* entity — a type, a function, an enumerator, a data member. That
// one-type-for-everything is why `find_location` and `find_primitive` have the
// same shape despite looking for very different things. `info` is a structural
// type, so it can be a non-type template parameter, which is the hinge the
// whole file turns on: `template<std::meta::info Fn>` makes "which function"
// part of a template instantiation's identity.
//
// **Splices**, `[: … :]`, turn an `info` back into code. They look like one
// feature and are four, each with its own grammar:
//
//   typename[: type_of(p) :]           a type. The `typename` is mandatory:
//                                      the parser cannot know what a splice
//                                      yields until it is instantiated.
//   [:Fn:](arguments...)               a function, in callee position.
//   read(cpu, [:find_location(…):])    an enumerator, yielding a prvalue of the
//                                      enum type — so ordinary overload
//                                      resolution picks `read(…, R8)` or
//                                      `read(…, Bit)`. The framework does not
//                                      dispatch on the kind of location; C++
//                                      does, because the splice has a type.
//   result.[:members[at]:]             a data member. The leading `.` is not a
//                                      typo; it is member-access syntax with a
//                                      splice where the name would be.
//
// **Expansion statements (P1306)**, `template for`. The body is *instantiated
// once per element*, so it is code size rather than a loop, and the induction
// variable is `constexpr` inside the body — which is what lets it be used as a
// template argument. A `return` inside one returns from the enclosing function,
// not from an iteration. There is deliberately no `template switch`: an
// expansion statement generates statements, and a `case` label is not one, so a
// 256-way dispatch cannot be expanded into a `switch`. Hence a table of
// function pointers.
//
// **`consteval` functions that throw.** Nothing catches them. Throwing makes
// the call not a constant expression, and *that* is the diagnostic: a mistake
// in `z80.cpu` becomes a compile error carrying its line number. This is the
// most surprising idiom in the file, and it is used everywhere.
//
// **`std::define_static_array`.** `nonstatic_data_members_of` returns a
// `std::vector`, whose allocation cannot survive constant evaluation. This
// promotes the contents into an object with static storage duration, so a
// `span` over it *can* escape into a `constexpr` variable and still be usable
// as a template argument afterwards.
//
// **`std::meta::access_context::current()`** means the context of the function
// that names it — namespace scope here, not the caller's. Load-bearing twice:
// it is why `Flags` counts as one value rather than a struct to destructure
// (its byte is private, so this file cannot see it), and why the private
// helpers in `Ops` cannot be named by a table.
//
// ---------------------------------------------------------------------------
// Why the data looks the way it does
// ---------------------------------------------------------------------------
//
// `Call` and `Operand` are non-type template parameters, so they must be
// *structural*: literal types whose members are all public, recursively. That
// single requirement explains a lot of `Table.hpp` — why `Vector` exposes its
// `storage` and `count`, and why `Name` is a fixed `std::array<char, 15>`
// rather than a `std::string_view` (which has private members and is not
// structural).

// The table is written the way assembler is written, so every name in it is
// matched without regard to case.
[[nodiscard]] constexpr bool same_ignoring_case(const std::string_view lhs, const std::string_view rhs) {
  constexpr auto fold = [](const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
  return std::ranges::equal(lhs, rhs, {}, fold, fold);
}

// Every name a table uses must resolve to exactly one thing. Throwing from a
// `consteval` function is how a bad name becomes a compile error naming the
// line of `z80.cpu` that wrote it.
[[nodiscard]] consteval std::meta::info only_match(
    const std::span<const std::meta::info> candidates, const std::string_view name, const std::size_t line) {
  if (candidates.empty())
    throw table_error(line, "this CPU has nothing named '" + std::string(name) + "'");
  if (candidates.size() > 1)
    throw table_error(line, "this CPU has more than one thing named '" + std::string(name) + "'");
  return candidates.front();
}

// `a`, `hl`, `carry`, `pc`: an enumerator in one of the scopes the CPU offers.
// The `std::vector` here is fine — it is created and destroyed entirely within
// one constant evaluation, which is allowed; what it must not do is escape.
[[nodiscard]] consteval std::meta::info find_location(const std::string_view name, const std::size_t line) {
  std::vector<std::meta::info> candidates;
  for (const auto scope: location_scopes())
    for (const auto enumerator: std::meta::enumerators_of(scope))
      if (same_ignoring_case(std::meta::identifier_of(enumerator), name))
        candidates.push_back(enumerator);
  return only_match(candidates, name, line);
}

// `inc8`, `add16`, `is_set`: a static member function of one of the CPU's
// primitive scopes.
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

// Reflection stays in template arguments and alias templates, never in a local
// variable. Two separate reasons, and the first bites first:
//
//   * a `constexpr auto x = parameters_of(Fn);` in a function body makes that
//     function *immediately* evaluated -- the standard calls the initialiser an
//     immediate-escalating expression -- so the enclosing function becomes
//     `consteval` and can no longer be called with a running CPU;
//   * `parameters_of` returns a `std::vector`, whose storage cannot outlive the
//     constant evaluation that made it, so it could not be kept anyway.
//
// Written this way the values live in the template system, which also memoises
// them for free.
template<std::meta::info Fn>
inline constexpr std::size_t arity_of = std::meta::parameters_of(Fn).size();

// The `typename` is required: a splice's category is not known until it is
// instantiated, so the parser has to be told this one names a type.
template<std::meta::info Fn, std::size_t I>
using parameter_type = typename[:std::meta::type_of(std::meta::parameters_of(Fn)[I]):];

// What a result destructures into. Returns nothing for a non-class type, and
// also for a class whose members are all inaccessible from here --
// `access_context::current()` is this namespace, so a type keeping its state
// private looks empty. `apply` reads an empty answer as "one value, stored
// whole", which is what makes such a type a value rather than a pair.
[[nodiscard]] consteval std::span<const std::meta::info> destructures_into(const std::meta::info type) {
  if (!std::meta::is_class_type(type))
    return {};
  return std::define_static_array(std::meta::nonstatic_data_members_of(type, std::meta::access_context::current()));
}

// A primitive may ask for the machine itself, and if it does it must ask first:
// the framework supplies argument zero and the row supplies the rest, so which
// argument is which stays a property of the signature rather than of the row.
template<std::meta::info Fn>
[[nodiscard]] consteval bool takes_cpu() {
  if constexpr (arity_of<Fn> == 0)
    return false;
  else
    return std::is_same_v<parameter_type<Fn, 0>, Cpu &>;
}

// Everything one step needs, with its field references already resolved. This
// is a non-type template parameter, so every member of it — and of everything
// it contains — has to be public. See the note on structural types above.
struct Call {
  Vector<Operand, max_operands> operands{};
  Vector<Operand, max_operands> destinations{};
  std::size_t line{};
};

// An operand becomes the type the parameter it feeds asks for. A constant is
// checked here, because the table wrote it and a value too big for its
// parameter is a mistake worth naming. A location converts the ordinary way, so
// whether a 16-bit register reaching an 8-bit parameter is diagnosed depends on
// the build's warnings rather than on anything this file does.
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
    // An *enumerator* splice: this yields a prvalue whose type is the enum the
    // name was found in, so the CPU's overload set decides what reading it
    // means. `read(cpu, R8::A)` and `read(cpu, Bit::carry)` are different
    // functions returning different types, chosen here by nothing more exotic
    // than overload resolution.
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
//
// [dcl.init.list] says the guarantee survives CTAD and constructor selection,
// which is the part worth checking rather than assuming. Storing needs no such
// rescue: `template for` sequences its iterations, so destinations were never
// at risk.

// The generic-lambda-plus-`index_sequence` dance is here because this is the
// one job `template for` cannot do: expanding into a *call's argument list*
// needs a pack, and an expansion statement produces statements, not pack
// elements. The two C++26 features do not substitute for each other here.
template<std::meta::info Fn, Call C>
[[nodiscard]] auto operands_of(Cpu &cpu, const std::uint16_t immediate, const std::uint16_t indexed) {
  constexpr std::size_t supplied = takes_cpu<Fn>() ? 1 : 0;
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return std::tuple{value_of<C.operands[I], C.line, parameter_type<Fn, I + supplied>>(cpu, immediate, indexed)...};
  }(std::make_index_sequence<C.operands.size()>{});
}

// Arguments are supplied positionally; destinations destructure the result in
// declaration order.
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

  // Unevaluated, despite everything just said about operands having effects:
  // `decltype` asks for the type and calls nothing.
  using Result = decltype(call(operands_of<Fn, C>(cpu, immediate, indexed)));
  // `static` so the span's referent outlives this instantiation, which is what
  // lets `members[at]` below be a constant expression inside a splice.
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
    // More than one *destination* is how an instruction writes one result to
    // two places -- `dd cb d op` puts it through the addressing mode and into
    // the register its low bits name.
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

// One row, fully unrolled: every step spliced in, in order, with nothing of the
// table surviving into the generated code. There is one of these per (table,
// opcode) — 1792 for a complete Z80 — and each is typically a handful of
// instructions, because every choice below is made at compile time.
//
// `Table` and `Opcode` are template parameters rather than arguments precisely
// so that `rows[Index]`, the vocabulary lookups, and the renaming rules are all
// constants here.
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
  // A `constexpr std::optional` used two ways: contextually converted to `bool`
  // by `if constexpr`, and then dereferenced to give a template argument. Both
  // work because `optional`'s members are `constexpr`.
  constexpr auto displaced = displaced_through(fields, row, Opcode, rules);
  constexpr bool entered_latched = latched[Table];
  const std::uint8_t displacement = row.reads_displacement || (displaced && !entered_latched)
                                        ? static_cast<std::uint8_t>(fetch_immediate(cpu, 1))
                                        : latch;
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
      constexpr auto read_inside = row.immediate_bytes + (entered_latched ? 1 : 0);
      static_assert(
          read_inside <= 1, "this row reads more inside the window that forms its address than the window can hold");
      return displaced_address(cpu, direct_value_of<*displaced, row.line, std::uint16_t>(cpu, immediate), displacement,
          static_cast<std::uint8_t>(read_inside));
    }
    else
      return 0;
  }();
  // Expanded, not looped: the body is instantiated once per step, and `at` is
  // `constexpr` inside it — which is what lets `step` be a constant and its
  // contents be template arguments. `iota` rather than `row.steps` directly
  // because the index is wanted, and a `return` here leaves `execute_one`, not
  // the expansion.
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

// Every table is total -- `check_tables_total` insists on it -- so there is
// always a row here, which is why this dereferences without asking.
template<std::uint8_t Table, std::uint8_t Opcode>
inline constexpr Handler handler_for = &execute_one<Table, Opcode, *find_row(Table, Opcode)>;

// The clearest demonstration in the file of what an expansion statement buys:
// `handler_for<Table, opcode>` needs `opcode` as a *template argument*, so an
// ordinary loop cannot build this table and a `template for` can.
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
    const auto next = dispatches[table][opcode](cpu, latch);
    if (!next)
      return;
    table = next->table;
    latch = next->displacement;
  }
}

} // namespace specbolt::v4
