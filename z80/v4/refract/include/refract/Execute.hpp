#pragma once

#ifndef SPECBOLT_MODULES
// The consumer provides this: it must define `Cpu`, the scope functions, and
// the table constants this generates from. See Machine.hpp for the contract.
#include "refract_binding.hpp"

#include "refract/Machine.hpp"

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

namespace specbolt::refract {

// The machine this build generates for, named once here rather than spelled
// out at every use.
using Cpu = target::Cpu;

// The machine this build generates for. `Cpu` and the functions below come from
// the CPU description the consumer includes; checking the contract here means a
// machine missing one of them is told which, rather than finding out inside a
// generated instruction three hundred lines away.
static_assert(Machine<Cpu>, "this machine does not supply everything the framework needs; see Machine.hpp");
static_assert(
    requires { target::operation_scopes(); },
    "the target must say where a description's operation names are to be resolved");

// This file turns a parsed instruction table into an interpreter. `Table.hpp`
// has already read the description and lowered it to `constexpr` data; nothing here
// parses anything. What is left is to look up the names that data holds in the
// CPU description, and to emit one function per (table, opcode).
//
// Reading order, roughly top to bottom:
//
//   find_location / find_operation   a name in the table -> an entity in C++
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
// denote *any* entity: a type, a function, an enumerator, a data member. That
// one-type-for-everything is why `find_location` and `find_operation` have the
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
//   cpu.read([:find_location(…):])     an enumerator, yielding a prvalue of the
//                                      enum type, so ordinary overload
//                                      resolution picks whichever `read` that
//                                      kind of location has. The framework does
//                                      not dispatch on the kind of location;
//                                      C++ does, because the splice has a type.
//   result.[:members[at]:]             a data member. The leading `.` is not a
//                                      typo; it is member-access syntax with a
//                                      splice where the name would be.
//
// **Expansion statements (P1306)**, `template for`. The body is *instantiated
// once per element*, so it is code size rather than a loop, and the induction
// variable is `constexpr` inside the body, which is what lets it be used as a
// template argument. A `return` inside one returns from the enclosing function,
// not from an iteration. There is deliberately no `template switch`: an
// expansion statement generates statements, and a `case` label is not one, so a
// 256-way dispatch cannot be expanded into a `switch`. Hence a table of
// function pointers.
//
// **`consteval` functions that throw.** Nothing catches them. Throwing makes
// the call not a constant expression, and *that* is the diagnostic: a mistake
// in the description becomes a compile error carrying its line number. This is
// most surprising idiom in the file, and it is used everywhere.
//
// **`std::define_static_array`.** `nonstatic_data_members_of` returns a
// `std::vector`, whose allocation cannot survive constant evaluation. This
// promotes the contents into an object with static storage duration, so a
// `span` over it *can* escape into a `constexpr` variable and still be usable
// as a template argument afterwards.
//
// **`std::meta::access_context::current()`** means the context of the function
// that names it, namespace scope here rather than the caller's. Load-bearing twice:
// it is why asking what a result decomposes into gives the same answer here as
// a structured binding would give anywhere (a machine's flags type keeps its
// byte private, so it is one value and not a pair), and why a private helper in
// an operation scope cannot be named by a table.
//
// ---------------------------------------------------------------------------
// Why the data looks the way it does
// ---------------------------------------------------------------------------
//
// `Call` and `Resolved` are non-type template parameters, so they must be
// *structural*: literal types whose members are all public, recursively. That
// single requirement explains a lot of the model: why `Vector` exposes its
// `storage` and `count`, and why `Name` is a fixed `std::array<char, 15>`
// rather than a `std::string_view` (which has private members and is not
// structural).
//
// It is also why the parse cannot simply hand its `std::vector`s over:
// `std::define_static_array` would promote them, but only for a structural
// element type, and a `Row` holds `std::string_view`s. `ToArray.hpp` is what
// stands in its place.

// Where a location name may come from: the machine's own `read` overloads. A
// location is a thing the machine can read, so the pool is not a list, a
// namespace or an annotation but the capability itself. An enum with no `read`
// taking it is not a location, which is why a bus cycle kind cannot be one.
//
// `read_memory` is excluded by name; an overload taking more than the location
// is excluded by arity.
[[nodiscard]] consteval std::vector<std::meta::info> location_scopes() {
  std::vector<std::meta::info> scopes;
  for (const auto member: std::meta::members_of(^^Cpu, std::meta::access_context::current())) {
    if (!std::meta::is_function(member) || !std::meta::has_identifier(member))
      continue;
    if (std::meta::identifier_of(member) != read_verb)
      continue;
    const auto parameters = std::meta::parameters_of(member);
    if (parameters.size() != 1)
      continue;
    if (const auto type = std::meta::type_of(parameters[0]); std::meta::is_enum_type(type))
      scopes.push_back(type);
  }
  return scopes;
}

// Where a *value* may come from, for a vocabulary that names its scope: any
// enum an operation takes. Derived the same way and for the same reason, from
// what the CPU can be asked to do rather than from anything it declares about
// itself.
[[nodiscard]] consteval std::vector<std::meta::info> named_scopes() {
  auto scopes = location_scopes();
  for (const auto scope: target::operation_scopes())
    for (const auto member: std::meta::members_of(scope, std::meta::access_context::current())) {
      if (!std::meta::is_function(member))
        continue;
      for (const auto parameter: std::meta::parameters_of(member))
        if (const auto type = std::meta::type_of(parameter);
            std::meta::is_enum_type(type) && !std::ranges::contains(scopes, type))
          scopes.push_back(type);
    }
  return scopes;
}

// Every location name means exactly one thing, checked over the whole pool
// rather than as each name happens to be looked up. `only_match` would catch an
// ambiguity, but only for a name some description writes; this makes it a
// property of the machine, so a CPU that grows a second `carry` is told at once
// rather than whenever a row first wants one.
[[nodiscard]] consteval bool location_names_are_unique() {
  std::vector<std::string> seen;
  for (const auto scope: location_scopes())
    for (const auto enumerator: std::meta::enumerators_of(scope)) {
      auto name = std::string(std::meta::identifier_of(enumerator));
      for (auto &character: name)
        character = character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
      if (std::ranges::contains(seen, name))
        return false;
      seen.push_back(name);
    }
  return true;
}

static_assert(location_names_are_unique(),
    "two of this machine's readable locations are spelled the same, so a description could not say which it meant");

// The table is written the way assembler is written, so every name in it is
// matched without regard to case.
[[nodiscard]] constexpr bool same_ignoring_case(const std::string_view lhs, const std::string_view rhs) {
  constexpr auto fold = [](const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
  return std::ranges::equal(lhs, rhs, {}, fold, fold);
}

// Every name a table uses must resolve to exactly one thing. Throwing from a
// `consteval` function is how a bad name becomes a compile error naming the
// line of the description that wrote it.
[[nodiscard]] consteval std::meta::info only_match(
    const std::span<const std::meta::info> candidates, const std::string_view name, const std::size_t line) {
  if (candidates.empty())
    throw table_error(line, "this CPU has nothing named '" + std::string(name) + "'");
  if (candidates.size() > 1) {
    // Naming both is the point: the scopes are found by scanning rather than
    // listed, so "more than one" is most likely a scope the reader did not know
    // was being searched.
    std::string found;
    for (const auto candidate: candidates)
      found +=
          (found.empty() ? " (in " : ", ") + std::string(std::meta::identifier_of(std::meta::parent_of(candidate)));
    throw table_error(line, "this CPU has more than one thing named '" + std::string(name) + "'" + found + ")");
  }
  return candidates.front();
}

// An enumerator in one of the scopes the CPU offers, such as the Z80's `a`,
// `hl`, `carry` and `pc`.
// The `std::vector` here is fine, because it is created and destroyed within
// one constant evaluation, which is allowed; what it must not do is escape.
// What a description calls one enumerator: the `Spelling` it declares, or its
// own identifier when it declares none. The annotation is an override, so only
// a name the description and C++ disagree about has to be written down.
//
// `type_of` on an annotation is const-qualified, hence `^^const Spelling`.
[[nodiscard]] consteval std::string spelling_of(const std::meta::info enumerator) {
  for (const auto annotation: std::meta::annotations_of(enumerator))
    if (std::meta::type_of(annotation) == ^^const Spelling)
      return std::string(std::meta::extract<Spelling>(annotation).text.view());
  return std::string(std::meta::identifier_of(enumerator));
}

// The scope a vocabulary named. Compared exactly: it is a C++ type's name, not
// something written the way assembly is written.
[[nodiscard]] consteval std::meta::info find_scope(const std::string_view name, const std::size_t line) {
  std::string offered;
  for (const auto scope: named_scopes()) {
    if (std::meta::identifier_of(scope) == name)
      return scope;
    offered += (offered.empty() ? " (this CPU offers " : ", ") + std::string(std::meta::identifier_of(scope));
  }
  throw table_error(line, "no scope named '" + std::string(name) + "'" + offered + ")");
}

[[nodiscard]] consteval std::meta::info find_location(
    const std::string_view name, const std::size_t line, const std::string_view scope = {}) {
  std::vector<std::meta::info> candidates;
  if (!scope.empty()) {
    for (const auto enumerator: std::meta::enumerators_of(find_scope(scope, line)))
      if (same_ignoring_case(std::meta::identifier_of(enumerator), name) ||
          same_ignoring_case(spelling_of(enumerator), name))
        candidates.push_back(enumerator);
    return only_match(candidates, name, line);
  }
  for (const auto everywhere: location_scopes())
    for (const auto enumerator: std::meta::enumerators_of(everywhere))
      if (same_ignoring_case(std::meta::identifier_of(enumerator), name))
        candidates.push_back(enumerator);
  return only_match(candidates, name, line);
}

// An enumerator of the enum a *parameter* asks for. The parameter type is the
// scope, and that is what keeps these names out of the location namespace: a
// spelling is free to collide with the name of a register and mean something
// else entirely. (On the Z80, `i` and `d` are a direction here and the I and D
// registers everywhere else.)
[[nodiscard]] consteval std::meta::info find_spelling(
    const std::meta::info scope, const std::string_view name, const std::size_t line) {
  std::vector<std::meta::info> candidates;
  std::string offered;
  for (const auto enumerator: std::meta::enumerators_of(scope)) {
    const auto spelling = spelling_of(enumerator);
    offered += (offered.empty() ? " (it has " : ", ") + spelling;
    if (same_ignoring_case(spelling, name))
      candidates.push_back(enumerator);
  }
  if (candidates.empty())
    throw table_error(line, "no member of '" + std::string(std::meta::identifier_of(scope)) + "' is called '" +
                                std::string(name) + "'" + (offered.empty() ? ", which has no members" : offered + ")"));
  if (candidates.size() > 1)
    throw table_error(line, "more than one member of '" + std::string(std::meta::identifier_of(scope)) +
                                "' is called '" + std::string(name) + "'");
  return candidates.front();
}

// The locations a view selects between, in the order its vocabulary lists them,
// so that the view *is* the index. Every member resolves to a location of the
// same type, which `check_view_vocabulary` guarantees, so the machine
// is handed a location it already knows how to read, and needs no notion of a
// view at all. The alternative is for the machine to offer a location per
// vocabulary and a selector to go with it, which works only while the machine
// and the description agree about what the number means; nothing states that
// agreement, so nothing can check it.
//
// `template for` rather than a loop: a splice needs its operand to be a
// constant expression, and only an expansion statement's induction variable is
// one.
template<Resolved Op, std::size_t Line>
[[nodiscard]] consteval auto locations_of_view() {
  constexpr const auto &vocabulary = target::vocabularies[Op.view_vocabulary];
  constexpr const auto &members = vocabulary.members;
  // The scope comes from the vocabulary rather than from a member, because a
  // member is parsed before anything knows which vocabulary it will end up in.
  constexpr auto scope = vocabulary.scope;
  std::array<typename[:std::meta::type_of(find_location(members[0].operand.name.view(), Line, scope)):], members.size()>
      locations{};
  template for (constexpr auto at: std::views::iota(0uz, members.size()))
      locations[at] = [:find_location(members[at].operand.name.view(), Line, scope):];
  return locations;
}

// A static member function of one of the CPU's operation scopes, such as the
// Z80's `inc8`, `add16` and `is_set`.
[[nodiscard]] consteval std::meta::info find_operation(const std::string_view name, const std::size_t line) {
  std::vector<std::meta::info> candidates;
  for (const auto scope: target::operation_scopes())
    for (const auto member: std::meta::members_of(scope, std::meta::access_context::current()))
      // `has_identifier` excludes the implicitly-declared special members, which
      // have no name to compare. `is_static_member` excludes ordinary member
      // functions, which cannot be called without an object.
      if (std::meta::is_function(member) && std::meta::is_static_member(member) && std::meta::has_identifier(member) &&
          same_ignoring_case(std::meta::identifier_of(member), name))
        candidates.push_back(member);
  return only_match(candidates, name, line);
}

// Arity and parameter types live in the template system rather than in a local
// `constexpr`, and the reason is narrower than "reflection cannot go in a
// local": `takes_cpu` and `decomposes_into` are both called into locals
// further down, and both are fine.
//
// What is not fine is `parameters_of` specifically: it returns a `std::vector`,
// whose storage cannot outlive the evaluation that made it, so the initialiser
// is not a constant expression. A `consteval` call that is *not* a constant
// expression escalates: the standard promotes the enclosing templated function
// to `consteval` too, and it can then no longer be called with a running CPU.
// The vector is the cause and the escalation is the symptom.
//
// A variable template dodges it, and memoises the answer for free.
template<std::meta::info Fn>
inline constexpr std::size_t arity_of = std::meta::parameters_of(Fn).size();

// The `typename` is required: a splice's category is not known until it is
// instantiated, so the parser has to be told this one names a type.
template<std::meta::info Fn, std::size_t I>
using parameter_type = typename[:std::meta::type_of(std::meta::parameters_of(Fn)[I]):];

// The parts a result comes apart into, or nothing if it does not come apart.
//
// The rule is the language's: a class decomposes only when all of its
// non-static data members are public members of *that same class*
// ([dcl.struct.bind]). Asking with reflection rather than by writing
// `auto [a, b] =` means enforcing both halves here, because
// `nonstatic_data_members_of` answers a subtly different question twice over.
// It reports what is *accessible from the asking context*, which is not "all of
// them are public", and it reports only *direct* members, where a structured
// binding sees through a base class.
//
// Where the answers part company there is no safe guess: binding the visible
// half of a result would store some of it and drop the rest in silence. So a
// type all of whose state is hidden is one value, a type all of whose state is
// public is its parts, and anything in between is refused.
[[nodiscard]] consteval std::span<const std::meta::info> decomposes_into(
    const std::meta::info type, const std::size_t line) {
  if (!std::meta::is_class_type(type))
    return {};
  const auto visible = std::meta::nonstatic_data_members_of(type, std::meta::access_context::current());
  const auto every = std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked());
  if (visible.size() != every.size()) {
    if (!visible.empty())
      throw table_error(line, "this operation returns a type that hides some of its state and not the rest, so it is "
                              "neither one value nor a set of parts; the parts this row would be given are only the "
                              "ones this library can see");
    // Every member hidden: encapsulated, so one value, which is what the
    // language says too by refusing to decompose it.
    return {};
  }
  if (!std::meta::bases_of(type, std::meta::access_context::unchecked()).empty())
    throw table_error(line, "this operation returns a type with a base class, and a result is taken apart by its own "
                            "members, so whatever it inherits would be dropped");
  return std::define_static_array(visible);
}

// An operation may ask for the machine itself, and if it does it must ask first:
// the framework supplies argument zero and the row supplies the rest, so which
// argument is which stays a property of the signature rather than of the row.
template<std::meta::info Fn>
[[nodiscard]] consteval bool takes_cpu() {
  if constexpr (arity_of<Fn> == 0)
    return false;
  else
    return std::is_same_v<parameter_type<Fn, 0>, Cpu &>;
}

// What the instruction carries: the immediate its encoding fetched, the view a
// prefix chose, and the opcode itself. Fixed for the whole of one instruction.
//
// `indexed` is not here. It is computed by calling `direct_value_of`, so a
// struct holding it too would have to be built before one of its members
// existed. Its absence says it is derived rather than carried.
//
// Passed by value, and it must be: a handler ends in a tail call, and gcc
// refuses one from a frame whose contents have had their address taken.
struct Decoded {
  std::uint16_t immediate{};
  std::uint8_t view{};
  std::uint8_t opcode{};
};

// Everything one step needs, with its vocabulary references already resolved. This
// is a non-type template parameter, so every member of it, and of everything
// it contains, has to be public. See the note on structural types above.
struct Call {
  Vector<Resolved, max_operands> operands{};
  Vector<Resolved, max_operands> destinations{};
  // Which line of the description this came from, so a diagnostic can name it.
  // It sits here, and is threaded through `value_of`, `store` and the rest as a
  // separate template parameter, rather than being a member of `Resolved` where
  // it would obviously be tidier.
  //
  // Deliberately. `Resolved` is a template argument, so two of them are the same
  // argument when they are memberwise equal. Give it a line and an operand on
  // line 40 stops being the same one as the identical operand on line 90, every
  // instantiation below splits in two, and a file whose build cost is measured
  // in tens of seconds pays for a field that only ever appears in an error
  // message. The tidier arrangement is the expensive one.
  std::size_t line{};
};

// The rule the paragraph above states, said in a way the compiler checks. Give
// `Resolved` a `std::string_view` and this fires here, rather than as a
// deduction failure several hundred lines away from the cause.
static_assert(std::meta::is_structural_type(^^Name));
static_assert(std::meta::is_structural_type(^^Resolved));
static_assert(std::meta::is_structural_type(^^Call));

// An operand becomes the type the parameter it feeds asks for. A constant is
// checked here, because the table wrote it and a value too big for its
// parameter is a mistake worth naming. A location converts the ordinary way, so
// whether a 16-bit register reaching an 8-bit parameter is diagnosed depends on
// the build's warnings rather than on anything this file does.
template<Resolved Op, std::size_t Line, typename Parameter>
[[nodiscard]] Parameter direct_value_of(Cpu &cpu, const Decoded decoded) {
  static_assert(!std::is_reference_v<Parameter>,
      "an operation takes its operands by value; there is nothing here for a reference to bind to");
  if constexpr (Op.kind == Resolved::Kind::Constant && Op.from_opcode)
    // The instruction carries the number and the slice says where. Nothing to
    // check against the parameter: the mask already bounds it.
    return static_cast<Parameter>(Op.slice.extract(decoded.opcode));
  else if constexpr (Op.kind == Resolved::Kind::Constant) {
    // A parameter that is an enum has names for its values, and those names are
    // what a spelling annotation exists to expose. Casting a number into one
    // would get past every check the enum was introduced to impose, so this is
    // where a description is made to name a value rather than encode one.
    static_assert(!std::is_enum_v<Parameter>,
        "this parameter is an enum, so name one of its spellings rather than passing a number");
    if constexpr (std::integral<Parameter>)
      static_assert(Op.constant <= static_cast<std::uintmax_t>(std::numeric_limits<Parameter>::max()),
          "this constant does not fit the parameter it is passed to");
    return static_cast<Parameter>(Op.constant);
  }
  else if constexpr (Op.kind == Resolved::Kind::Immediate) {
    if constexpr (Op.width == 1)
      return static_cast<std::uint8_t>(decoded.immediate);
    else
      return decoded.immediate;
  }
  else if constexpr (Op.from_view) {
    // Which member is not known until the table's view has been chosen, so the
    // choice is an array index rather than a splice. See `locations_of_view`.
    static constexpr auto locations = locations_of_view<Op, Line>();
    return cpu.read(locations[decoded.view]);
  }
  else if constexpr (std::is_enum_v<Parameter>)
    // The name is one of the enum's members rather than a place to read from:
    // spliced as a value, with nothing fetched. Which enum comes from the
    // vocabulary if it named one, and otherwise from the parameter. Saying it
    // is what stops the meaning of a name depending on a signature elsewhere;
    // the parameter remains the answer for an operand no vocabulary owns, such
    // as one a member appends.
    return [:find_spelling(Op.scope.empty() ? ^^Parameter : find_scope(Op.scope.view(), Line), Op.name.view(), Line):];
  else
    // An *enumerator* splice: this yields a prvalue whose type is the enum the
    // name was found in, so the machine's overload set decides what reading it
    // means: on the Z80, `cpu.read(R8::A)` and `cpu.read(FlagBit::carry)` are
    // different functions returning different types, chosen here by nothing
    // more exotic than overload resolution.
    return cpu.read([:find_location(Op.name.view(), Line, Op.scope.view()):]);
}

// The address an indirect operand addresses through. A displaced one was formed
// once for the whole instruction, before any operand was touched.
template<Resolved Op, std::size_t Line>
[[nodiscard]] std::uint16_t address_of(Cpu &cpu, const Decoded decoded, const std::uint16_t indexed) {
  if constexpr (Op.displaced)
    return indexed;
  else
    return direct_value_of<Op, Line, std::uint16_t>(cpu, decoded);
}

// An indirect operand is whatever it would have been, read as an address. How
// wide the read is comes from the parameter it feeds rather than from anything
// the row says, so one operand spelling serves every width the machine offers.
// (On the Z80 that is `ld16 hl <- (n)` reading two bytes where `ld8 a <- (n)`
// reads one.)
template<Resolved Op, std::size_t Line, typename Parameter>
[[nodiscard]] Parameter value_of(Cpu &cpu, const Decoded decoded, const std::uint16_t indexed) {
  if constexpr (Op.indirect) {
    // The machine offers two widths and the parameter's type picks. Said out
    // loud because the alternative is an `else` that quietly means "one byte":
    // an operation declaring `unsigned` rather than `std::uint16_t` would read
    // half of what it asked for and zero-extend the rest.
    static_assert(std::same_as<Parameter, std::uint8_t> || std::same_as<Parameter, std::uint16_t>,
        "an indirect operand is read at one of the two widths the machine offers");
    const auto address = address_of<Op, Line>(cpu, decoded, indexed);
    if constexpr (std::same_as<Parameter, std::uint16_t>)
      return cpu.read_memory16(address);
    else
      return cpu.read_memory(address);
  }
  else
    return direct_value_of<Op, Line, Parameter>(cpu, decoded);
}

template<Resolved Op, std::size_t Line, typename T>
void store(Cpu &cpu, const Decoded decoded, const std::uint16_t indexed, const T value) {
  if constexpr (Op.kind == Resolved::Kind::Discard)
    static_cast<void>(value);
  else if constexpr (Op.indirect) {
    static_assert(std::same_as<T, std::uint8_t> || std::same_as<T, std::uint16_t>,
        "an indirect destination is written at one of the two widths the machine offers");
    // The addressing mode says how long the machine idles before writing back.
    if constexpr (Op.write_back_delay != 0)
      cpu.delay(Op.write_back_delay);
    const auto address = address_of<Op, Line>(cpu, decoded, indexed);
    if constexpr (std::same_as<T, std::uint16_t>)
      cpu.write_memory16(address, value);
    else
      cpu.write_memory(address, value);
  }
  else {
    static_assert(Op.kind == Resolved::Kind::Named, "only a named location can be a destination");
    if constexpr (Op.from_view) {
      static constexpr auto locations = locations_of_view<Op, Line>();
      cpu.write(locations[decoded.view], value);
    }
    else
      cpu.write([:find_location(Op.name.view(), Line, Op.scope.view()):], value);
  }
}

// Resolving an operand is not a pure act: it can read memory, advance the clock
// and move the address bus. So the order matters, and the order a function's
// arguments are evaluated in is *unspecified*, and gcc evaluates them right to
// left. Braced initialisation is sequenced left to right ([dcl.init.list]/4),
// so the values are materialised into a tuple first and the call made from
// that.
//
// The rows that prove this is not pedantry are the ones that read memory and
// then ask for the address that read left on the bus. The Z80's `bit n, (ix+d)`
// is one, and getting those two the wrong way round takes its undocumented
// flags from the wrong place.
//
// [dcl.init.list] says the guarantee survives CTAD and constructor selection,
// which is the part worth checking rather than assuming. Storing needs no such
// rescue: `template for` sequences its iterations, so destinations were never
// at risk.

// Which of the row's operands feeds each of the operation's parameters. By
// position, unless the row said otherwise: an operand written `value=…` goes to
// the parameter *called* `value`, and what the parameters are called is asked
// of the declaration rather than written down anywhere.
//
// This exists because position is a silent coupling. An operation taking
// several parameters of one type, as the Z80's `bit8(value, bit, flags, bus)`
// takes three `std::uint8_t`s, lets a row swap two of them and still compile,
// run, and quietly test the wrong bit.
//
// Naming is all or nothing within a step. A half-named argument list needs a
// rule about what "the next one" means, and a description is easier to read if
// there is no such rule to remember.
template<std::meta::info Fn, Call C>
[[nodiscard]] consteval std::array<std::size_t, C.operands.size()> operand_for_parameter() {
  constexpr std::size_t supplied = takes_cpu<Fn>() ? 1 : 0;
  std::array<std::size_t, C.operands.size()> written{};
  std::size_t named = 0;
  for (const auto &operand: C.operands)
    if (!operand.parameter.empty())
      ++named;
  if (named == 0) {
    for (std::size_t at = 0; at < written.size(); ++at)
      written[at] = at;
    return written;
  }
  if (named != C.operands.size())
    throw table_error(C.line,
        "this step names some of its parameters and not others; name all of them or none, so that reading it needs no "
        "rule about which is which");

  const auto parameters = std::meta::parameters_of(Fn);
  std::string offered;
  for (std::size_t slot = 0; slot < written.size(); ++slot) {
    const auto parameter = parameters[slot + supplied];
    if (!std::meta::has_identifier(parameter))
      throw table_error(
          C.line, "this operation was declared without parameter names, so there is nothing to name here");
    offered += (offered.empty() ? " (it takes " : ", ") + std::string(std::meta::identifier_of(parameter));
  }
  for (std::size_t slot = 0; slot < written.size(); ++slot) {
    const auto name = std::meta::identifier_of(parameters[slot + supplied]);
    std::size_t found = 0;
    std::size_t matches = 0;
    for (const auto [at, operand]: std::views::enumerate(C.operands))
      if (same_ignoring_case(operand.parameter.view(), name)) {
        found = static_cast<std::size_t>(at);
        ++matches;
      }
    if (matches == 0)
      throw table_error(C.line, "no operand is given for '" + std::string(name) + "'" + offered + ")");
    if (matches > 1)
      throw table_error(C.line, "'" + std::string(name) + "' is given more than one operand");
    written[slot] = found;
  }
  return written;
}

// The inverse: which parameter each operand, in the order the row wrote it,
// ends up feeding. Needed because an operand is *read* where the row put it and
// *passed* where the signature wants it, and its type comes from the latter.
template<std::meta::info Fn, Call C>
[[nodiscard]] consteval std::array<std::size_t, C.operands.size()> parameter_for_operand() {
  constexpr auto operand = operand_for_parameter<Fn, C>();
  std::array<std::size_t, C.operands.size()> parameter{};
  for (std::size_t slot = 0; slot < operand.size(); ++slot)
    parameter[operand[slot]] = slot;
  return parameter;
}

// The row's operands, resolved in the order the row wrote them, because
// resolving one can read memory and move the address bus. Naming a parameter
// changes which argument an operand becomes, never when it is read.
//
// The generic-lambda-plus-`index_sequence` dance is here because this is the
// one job `template for` cannot do: expanding into a *call's argument list*
// needs a pack, and an expansion statement produces statements, not pack
// elements. The two C++26 features do not substitute for each other here.
template<std::meta::info Fn, Call C>
[[nodiscard]] auto operands_of(Cpu &cpu, const Decoded decoded, const std::uint16_t indexed) {
  constexpr std::size_t supplied = takes_cpu<Fn>() ? 1 : 0;
  constexpr auto parameter = parameter_for_operand<Fn, C>();
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return std::tuple{
        value_of<C.operands[I], C.line, parameter_type<Fn, parameter[I] + supplied>>(cpu, decoded, indexed)...};
  }(std::make_index_sequence<C.operands.size()>{});
}

// Where the row's order and the signature's order are reconciled: the tuple
// holds the values in the order they were read, and this hands them over in the
// order the parameters want them.
template<std::meta::info Fn, Call C>
[[nodiscard]] auto call_with(Cpu &cpu, const auto &arguments) {
  constexpr auto operand = operand_for_parameter<Fn, C>();
  return [&]<std::size_t... S>(std::index_sequence<S...>) {
    if constexpr (takes_cpu<Fn>())
      return [:Fn:](cpu, std::get<operand[S]>(arguments)...);
    else
      return [:Fn:](std::get<operand[S]>(arguments)...);
  }(std::make_index_sequence<C.operands.size()>{});
}

// One member of a returned struct. A splice in member-access position is
// legitimate and reads as a typo, so it appears once, here, rather than inline
// at the only place that wants it.
template<std::meta::info Member>
[[nodiscard]] constexpr decltype(auto) member_of_result(const auto &result) {
  return result.[:Member:];
}

// Arguments are supplied positionally, or by name where the row said so;
// destinations destructure the result in declaration order.
template<std::meta::info Fn, Call C>
void apply(Cpu &cpu, const Decoded decoded, const std::uint16_t indexed) {
  constexpr std::size_t supplied = takes_cpu<Fn>() ? 1 : 0;
  static_assert(
      C.operands.size() + supplied == arity_of<Fn>, "the row supplies the wrong number of operands for this operation");
  // A default capture rather than `[&cpu]`, because only one branch of the
  // `if constexpr` names it: an operation that does not ask for the machine
  // leaves an explicit capture unused, which clang diagnoses and gcc does not.
  const auto call = [&](const auto &arguments) { return call_with<Fn, C>(cpu, arguments); };

  // Unevaluated, despite everything just said about operands having effects:
  // `decltype` asks for the type and calls nothing.
  using Result = decltype(call(operands_of<Fn, C>(cpu, decoded, indexed)));
  // A `std::span`, and safe to hold: `decomposes_into` promotes its contents
  // with `define_static_array`, so what this points at has static storage and
  // `members[at]` is a constant expression a splice can use.
  static constexpr auto members = decomposes_into(^^Result, C.line);
  // The language would decompose a one-member class quite happily; refusing to
  // is this framework's own policy, because such a result is as good a
  // description of one value as of a bundle holding one, and a row would be
  // written differently depending on which was meant.
  static_assert(members.size() != 1,
      "a result with one part is ambiguous: give it a second part, or keep its state to itself and be one value");
  if constexpr (std::is_void_v<Result>) {
    static_assert(C.destinations.size() == 0, "this operation returns nothing, so the row may not name a destination");
    call(operands_of<Fn, C>(cpu, decoded, indexed));
  }
  else if constexpr (members.size() > 1) {
    // Two is a value and the flags it set, which is what almost every
    // arithmetic operation returns. One accessible member is caught above as
    // ambiguous.
    static_assert(
        C.destinations.size() == members.size(), "the row's destinations do not match what this operation returns");
    const auto result = call(operands_of<Fn, C>(cpu, decoded, indexed));
    template for (constexpr auto at: std::views::iota(0uz, C.destinations.size()))
        store<C.destinations[at], C.line>(cpu, decoded, indexed, member_of_result<members[at]>(result));
  }
  else {
    static_assert(C.destinations.size() >= 1, "this operation returns a value, so the row must name a destination");
    // More than one *destination* is how an instruction writes one result to
    // two places, as the Z80's `dd cb d op` puts it through the addressing mode
    // and into the register its low bits name.
    const auto result = call(operands_of<Fn, C>(cpu, decoded, indexed));
    template for (constexpr auto destination: C.destinations) store<destination, C.line>(cpu, decoded, indexed, result);
  }
}

// A condition is applied like any other operation; only what is done with the
// answer differs.
template<std::meta::info Fn, Call C>
[[nodiscard]] bool evaluate(Cpu &cpu, const Decoded decoded, const std::uint16_t indexed) {
  static_assert(C.operands.size() == arity_of<Fn>, "the row supplies the wrong number of operands for this condition");
  static_assert(C.destinations.size() == 0, "a condition names no destination; it decides whether the rest happens");
  static_assert(!takes_cpu<Fn>(), "a condition may not ask for the machine; it only reads what the row hands it");
  static_assert(std::is_same_v<typename[:std::meta::return_type_of(Fn):], bool>, "a condition must answer yes or no");
  return call_with<Fn, C>(cpu, operands_of<Fn, C>(cpu, decoded, indexed));
}

// A vocabulary member may bind the operation late, and may append an operand the
// encoding does not carry.
[[nodiscard]] consteval Member member_for(
    const Step &step, const Pattern &matched, const std::uint8_t opcode, const Rules &rules) {
  if (!step.operation_reference)
    return {};
  return member_of({.vocabularies = target::vocabularies, .matched = matched, .rules = rules, .opcode = opcode},
      *step.operation_reference);
}

[[nodiscard]] consteval Call call_for(
    const Step &step, const Pattern &matched, const std::uint8_t opcode, const std::size_t line, const Rules &rules) {
  const auto member = member_for(step, matched, opcode, rules);
  const Resolution at{.vocabularies = target::vocabularies, .matched = matched, .rules = rules, .opcode = opcode};
  Call result{.line = line};
  for (const auto &operand: step.operands)
    if (!result.operands.try_push_back(resolve(at, operand)))
      throw table_error(line, "too many operands");
  for (const auto &written: step.destinations) {
    auto destination = resolve(at, written);
    // The idle cycle belongs to a write-back, so only to something read through
    // the same address it will be written through.
    const auto was_read = destination.indirect && std::ranges::any_of(result.operands, [&](const Resolved &operand) {
      return operand.indirect && operand.name == destination.name;
    });
    if (!was_read)
      destination.write_back_delay = 0;
    if (!result.destinations.try_push_back(destination))
      throw table_error(line, "too many destinations");
  }
  // A vocabulary member may append an operand the encoding does not carry. It
  // named no vocabulary, so it is already resolved and has no scope: which enum
  // a name means here is the parameter's business.
  for (const auto &argument: member.arguments)
    if (!result.operands.try_push_back(as_resolved(argument)))
      throw table_error(line, "too many operands");
  return result;
}

// What a row says to do once it has run: nothing, or fetch another byte and
// decode it in the table named. A prefix *returns* where to go rather than
// going there, because `dd dd dd ...` is a legal and unbounded Z80 instruction:
// it must cost a fetch a byte, not a stack frame a byte. The displacement rides
// along because `dd cb d op` reads its displacement one table before the row
// that uses it.
// A handler does not report where to go next; it goes there. A `goto` step ends
// in a tail call to the next table's handler, so a prefix chain is one call
// deep however long it is, and `dd dd dd ...` no more grows the stack than it
// grows the instruction.
//
// The displacement and the view travel as arguments for the reason they always
// did: both are chosen by the prefix, needed by the row, and in neither's own
// bytes.
// Every handler has one signature, because a table of function pointers can
// only have one. So `view` is a parameter of every handler and not merely of
// the ones a prefix can reach: a machine's simplest instruction pays a
// register's worth for its most elaborate addressing mode existing. That trade
// has been measured, and the alternative rejected, in the design journal.
using Handler = void (*)(Cpu &, std::uint8_t latch, std::uint8_t view, std::uint8_t opcode);

// A handler tail-calls into another table's dispatch, and a dispatch is built
// out of handlers, so one of the two has to be named before it is defined. A
// function template can be; the variable template it returns cannot.
template<std::uint8_t Table>
[[nodiscard]] const std::array<Handler, 256> &dispatch_for();

void continue_running(Cpu &cpu, std::uint8_t latch, std::uint8_t view, std::uint8_t opcode);

// One row, fully unrolled: every step spliced in, in order, with nothing of the
// table surviving into the generated code. There is one of these per *body*, a
// row together with the slices it reads, so every opcode of a row that reads
// none of its variable bits shares one; `body_key` is what decides. Each is
// typically a handful of instructions, because every choice below is made at
// compile time.
//
// `Table` and `Opcode` are template parameters rather than arguments precisely
// so that `target::rows[Index]`, the vocabulary lookups, and the renaming rules are all
// constants here.
template<std::uint8_t Table, std::uint8_t Opcode, std::size_t Index>
void execute_one(Cpu &cpu, const std::uint8_t latch, const std::uint8_t view, const std::uint8_t opcode) {
  // `static` is not an optimisation here: the expansion statement below walks
  // this as a range, and a range's *address* has to be a constant. A local
  // `constexpr` has a constant value but not a constant address.
  static constexpr auto row = target::rows[Index];
  // A renaming applies to every row this table decodes, inherited or its own. A
  // rule names the vocabulary it rewrites, not just the member, so a row can opt
  // out of a renaming by naming a vocabulary no rule mentions. (That is how the
  // Z80's `ld {real:y}, (ix+d)` keeps a real h.)
  static constexpr auto rules = target::tables[Table].rules;
  // The displacement is read before any immediate, which is the order the bytes
  // appear in, as the Z80's `dd 36 d n` spells `ld (ix+d), n`.
  // Unless this table was entered with a displacement already read, in which
  // case it arrived before this row's own opcode did.
  // A `constexpr std::optional` used two ways: contextually converted to `bool`
  // by `if constexpr`, and then dereferenced to give a template argument. Both
  // work because `optional`'s members are `constexpr`.
  // `static` for the same reason `row` is: a tail call abandons the frame, so
  // anything the compiler thinks lives in it blocks one.
  static constexpr auto displaced = displaced_through(target::vocabularies, row, Opcode, rules);
  constexpr bool entered_latched = target::latched[Table];
  const std::uint8_t displacement = row.reads_displacement || (displaced && !entered_latched)
                                        ? static_cast<std::uint8_t>(cpu.fetch_immediate(1))
                                        : latch;
  // A `goto` is the whole of its row: a prefix reads no operands and has no
  // immediate, so nothing below this line applies to one. It is also why the
  // hand-over happens *here* rather than among the steps: a tail call abandons
  // the frame, and gcc will not allow one out of a function whose locals have
  // had their address taken. The lambda that forms `indexed` takes several.
  if constexpr (row.steps.size() == 1 && row.steps[0].kind == Step::Kind::Goto) {
    constexpr auto step = row.steps[0];
    constexpr std::uint8_t next_table = step.target;
    const auto next_view = static_cast<std::uint8_t>(step.forwards_view ? view : step.target_view);
    // The fetch the loop used to do, now done by whoever hands over. A latched
    // table's opcode arrives as an operand read rather than an instruction
    // fetch, which is cheaper and does not refresh.
    const auto next_opcode =
        static_cast<std::uint8_t>(target::latched[next_table] ? cpu.fetch_immediate(1) : cpu.fetch_opcode());
    [[gnu::musttail]] return dispatch_for<next_table>()[next_opcode](cpu, displacement, next_view, next_opcode);
  }
  else {

    // The encoding column says what is fetched, and it is fetched once before any
    // step: argument order within a call is unspecified, and a later step may
    // store through an address an earlier one read.
    const std::uint16_t immediate = row.immediate_bytes == 0 ? 0 : cpu.fetch_immediate(row.immediate_bytes);
    // Formed once, after both, and handed to every operand that shares it. The
    // machine is told what else was read first, because on a Z80 those reads
    // happen *inside* the window that forms the address rather than before it.
    const Decoded decoded{.immediate = immediate, .view = view, .opcode = opcode};
    const std::uint16_t indexed = [&cpu, decoded, displacement] -> std::uint16_t {
      if constexpr (displaced) {
        // A latched table read its opcode inside the same window, so that byte
        // counts too, and the machine is charged for the window once rather
        // than for each read inside it.
        //
        // The machine is told the count and works out what is left of the window
        // from it, so a count the window cannot hold asks it for a negative delay.
        // Caught here, where the number is a constant, rather than at run time as
        // an enormous unsigned one.
        constexpr auto read_inside = row.immediate_bytes + (entered_latched ? 1 : 0);
        static_assert(
            read_inside <= 1, "this row reads more inside the window that forms its address than the window can hold");
        return cpu.displaced_address(direct_value_of<*displaced, row.line, std::uint16_t>(cpu, decoded), displacement,
            static_cast<std::uint8_t>(read_inside));
      }
      else
        return 0;
    }();
    // Expanded, not looped: the body is instantiated once per step, and `step` is
    // `constexpr` inside it, which is what lets its contents be template
    // arguments. A `return` here leaves `execute_one`, not the expansion.
    template for (constexpr auto step: row.steps) {
      {
        constexpr auto member = member_for(step, row.matched, Opcode, rules);
        constexpr auto operation = step.operation_reference ? member.operation : step.operation;
        constexpr auto call = call_for(step, row.matched, Opcode, row.line, rules);
        if constexpr (step.kind == Step::Kind::If) {
          // The rest of the row is the conditional half, which is where the
          // extra cycles of a taken branch come from too. `break` rather than
          // `return`, because abandoning the rest of a row is not abandoning
          // the run: the hand-over below still has to happen, and a `return`
          // here stops the machine at the first untaken branch. It cannot tail
          // call from in here either, since the expansion's own induction
          // variable lives in the frame a tail call would abandon.
          if (!evaluate<find_operation(operation, row.line), call>(cpu, decoded, indexed))
            break;
        }
        else
          apply<find_operation(operation, row.line), call>(cpu, decoded, indexed);
      }
    }
  }
  // The row is done, so hand on to the next instruction rather than returning.
  // This is the whole of the run loop: it used to be a `while` in the caller.
  [[gnu::musttail]] return continue_running(cpu, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// One function per instruction, not one per (table, opcode)
// ---------------------------------------------------------------------------
//
// A row's body is built only from the slices it *reads*. A catch-all that reads
// none of its opcodes is one instruction wearing as many hats as it claims; a
// row reading two three-bit slices is genuinely sixty-four instructions.
// Generating per (table, opcode) cannot tell the difference and stamps out 256
// either way.
//
// So walk rows and splat each body across the opcodes it claims. Nothing is
// deduplicated because nothing is generated twice: the combinations of the
// slices a row reads enumerate its distinct bodies exactly once, and the fill
// is a direct write rather than a lookup.

// Which of a row's slices change the generated code. Three kinds do not: a
// view is a run-time value, a numeric vocabulary is read straight out of the
// opcode, and the mnemonic's own references are the disassembler's business;
// nothing below this line ever looks at `row.pieces`.
[[nodiscard]] consteval Vector<std::uint8_t, Pattern::max_slices> slices_read_by(const Row &row) {
  Vector<std::uint8_t, Pattern::max_slices> used;
  const auto note = [&used](const Reference reference) {
    if (reference.from_view || is_numeric(target::vocabularies[reference.vocabulary_index]))
      return;
    if (std::ranges::contains(used, reference.slice_index))
      return;
    // Cannot overflow: these are distinct slice indices of one pattern, and a
    // pattern holds at most `Pattern::max_slices` of them.
    static_cast<void>(used.try_push_back(reference.slice_index));
  };
  for (const auto &step: row.steps) {
    if (step.operation_reference)
      note(*step.operation_reference);
    for (const auto &operand: step.operands)
      if (operand.kind == Operand::Kind::Vocabulary)
        note(operand.reference);
    for (const auto &destination: step.destinations)
      if (destination.kind == Operand::Kind::Vocabulary)
        note(destination.reference);
  }
  return used;
}

// The encoding with every unread variable bit cleared, which names the body
// this opcode wants. Two opcodes of one row share a body exactly when this
// agrees.
[[nodiscard]] consteval std::uint8_t body_key(const Row &row, const std::uint8_t opcode) {
  auto result = row.matched.opcode_bits;
  for (const auto index: slices_read_by(row)) {
    const auto &slice = row.matched.slices[index];
    result = static_cast<std::uint8_t>(result | slice.place(slice.extract(opcode)));
  }
  return result;
}

// One generated function: a row, and an encoding fixing every slice it reads.
struct Body {
  std::size_t row{};
  std::uint8_t opcode{};
};

// What a table's dispatch is made of, in one pass over its rows.
struct Decoding {
  std::vector<Body> bodies;
  std::array<std::uint16_t, 256> fill{};
};

[[nodiscard]] consteval Decoding decoding_for(const std::uint8_t table) {
  Decoding result;
  // Indexed, never searched: `made[row][key]` is the body this row already has
  // for that combination of the slices it reads. Per row as well as per key,
  // because two rows may narrow to the same encoding and are still two rows.
  std::vector<std::array<std::optional<std::uint16_t>, 256>> made(target::rows.size());
  for (const auto opcode: std::views::iota(0uz, 256uz)) {
    const auto row = *target::find_row(table, static_cast<std::uint8_t>(opcode));
    const auto key = body_key(target::rows[row], static_cast<std::uint8_t>(opcode));
    auto &body = made[row][key];
    if (!body) {
      body = static_cast<std::uint16_t>(result.bodies.size());
      result.bodies.push_back({.row = row, .opcode = key});
    }
    result.fill[opcode] = *body;
  }
  return result;
}

template<std::uint8_t Table>
inline constexpr auto bodies_of = to_array<[] { return decoding_for(Table).bodies; }>();
template<std::uint8_t Table>
inline constexpr auto fill_of = decoding_for(Table).fill;

// The clearest demonstration in the file of what an expansion statement buys:
// `execute_one` needs its row and encoding as *template arguments*, so an
// ordinary loop cannot make these and a `template for` can. Filling the 256
// entries afterwards is an ordinary loop, because by then they are values.
template<std::uint8_t Table>
inline constexpr auto dispatch = [] {
  std::array<Handler, bodies_of<Table>.size()> made{};
  template for (constexpr auto at: std::views::iota(0uz, bodies_of<Table>.size())) made[at] =
      &execute_one<Table, bodies_of<Table>[at].opcode, bodies_of<Table>[at].row>;
  std::array<Handler, 256> handlers{};
  std::ranges::transform(fill_of<Table>, handlers.begin(), [&made](const std::uint16_t body) { return made[body]; });
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

inline constexpr auto dispatches = all_dispatches(std::make_index_sequence<target::tables.size()>{});

// Fetch, decode, run; and go round again while what ran was a prefix. Each turn
// of the loop is a real opcode fetch, so the loop always advances time and
// always advances PC, which is why a table may now reach itself.
template<std::uint8_t Table>
[[nodiscard]] const std::array<Handler, 256> &dispatch_for() {
  return dispatch<Table>;
}

// Where one instruction becomes the next. Every handler ends here, and this
// ends in the next handler, so a run of instructions is a chain of tail calls
// and the stack never grows. The machine decides whether there is a next one:
// `start_instruction` is where a Z80 takes its interrupt and idles its halt,
// none of which is the framework's business.
//
// Handler-shaped so that the tail call out of a handler is a tail call: the
// three arguments a fresh instruction has no use for are passed as zero.
inline void continue_running(Cpu &cpu, std::uint8_t, std::uint8_t, std::uint8_t) {
  if (!cpu.start_instruction())
    return;
  const auto opcode = cpu.fetch_opcode();
  [[gnu::musttail]] return dispatch_for<target::entry_table>()[opcode](cpu, 0, 0, opcode);
}

// The whole of the run loop. What used to be a `while (true)` around a dispatch
// is now the handlers themselves, and this only starts them off.
inline void execute_instruction(Cpu &cpu) { continue_running(cpu, 0, 0, 0); }

} // namespace specbolt::refract
