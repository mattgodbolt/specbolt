#pragma once

// Turns a compiled description into an interpreter for a machine. `Compiled`
// has already read the text and lowered it to `constexpr` data; nothing here
// parses anything. What is left is to look up the names that data holds in the
// machine, and to emit one function per body: a row together with the slices
// it reads, so that opcodes generating the same code share one.
//
// A target names the two, and the palettes its description may draw verbs
// from:
//
//   struct Target {
//     using Machine = ...;                       // see Machine.hpp
//     using Compiled = refract::Compiled<text, "file">;  // see Compiled.hpp
//     static consteval std::vector<std::meta::info> palettes();
//   };
//
// A palette is a type every public static function of which is a verb. The
// machine's own verbs are the members it publishes with
// `[[=refract::operation]]`, static or not; see Model.hpp.
//
// `Interpreter<Target>::run` then runs the machine until it says stop.

#include "refract/Coverage.hpp"
#include "refract/Machine.hpp"
#include "refract/Model.hpp"
#include "refract/TableError.hpp"
#include "refract/ToArray.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <limits>
#include <meta>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace specbolt::refract {

// Reading order, roughly top to bottom:
//
//   find_location / find_operation   a name in the table -> an entity in C++
//   direct_value_of / value_of       an operand -> a value, reading if it must
//   store                            a value -> an operand, writing if it must
//   operands_of / apply / evaluate   one step
//   execute_one                      one row: every step, unrolled
//   dispatch / run                   a table of those per table, and the loop
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
// **Splices**, `[: … :]`, turn an `info` back into code. One syntax, which
// denotes whatever the reflection designated; the forms here are
//
//   using T = [: type_of(p) :];        a type. In a template argument list the
//                                      same splice would be read as an
//                                      expression and needs `typename` in
//                                      front; an alias declaration does not.
//   [:Fn:](arguments...)               a function, in callee position.
//   machine.read([:find_location(…):]) an enumerator, a prvalue of its enum
//                                      type, so ordinary overload resolution
//                                      picks the `read` for that kind of
//                                      location. The framework never dispatches
//                                      on the kind of location; C++ does.
//   result.[:members[at]:]             a data member, in member-access position.
//
// **Expansion statements (P1306)**, `template for`. The body is *instantiated
// once per element*, so it is code size rather than a loop, and the induction
// variable is `constexpr` inside the body, which is what lets it be used as a
// template argument. A `return` inside one returns from the enclosing function,
// not from an iteration. There is deliberately no `template switch`: the body
// of an expansion statement is control-flow-limited, so a `case` label inside
// it can only belong to a `switch` that is also inside it, and a 256-way
// dispatch cannot be expanded into one. Hence a table of function pointers.
//
// **`consteval` functions that throw.** Nothing catches them. Throwing makes
// the call not a constant expression, and *that* is the diagnostic: a mistake
// in the description becomes a compile error carrying its line number. This is
// the most surprising idiom in the file, and it is used everywhere.
//
// **`std::define_static_array`.** `nonstatic_data_members_of` returns a
// `std::vector`, whose allocation cannot survive constant evaluation. This
// promotes the contents into an object with static storage duration, so a
// `span` over it *can* escape into a `constexpr` variable and still be usable
// as a template argument afterwards.
//
// **`std::meta::access_context::current()`** means the context of the function
// that names it, `Interpreter`'s own scope here rather than the caller's, and
// `Interpreter` is nobody's friend. Load-bearing twice:
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

template<typename Target>
struct Interpreter {
  using Machine = typename Target::Machine;
  using Compiled = typename Target::Compiled;

  // Checking the contract here means a machine missing one of its functions is
  // told which, rather than finding out inside a generated instruction three
  // hundred lines away.
  static_assert(MachineLike<Machine>, "this machine does not supply everything the framework needs; see Machine.hpp");
  static_assert(
      requires { Target::palettes(); }, "the target must list the palettes a description may draw its operations from");

  // A mistake in the description, reported against its line and the file the
  // target says it came from.
  [[nodiscard]] static consteval std::runtime_error error(const std::size_t line, const std::string_view what) {
    return table_error(Compiled::file, line, what);
  }

  // Where a location name may come from: the machine's own `read` overloads. A
  // location is a thing the machine can read, so the pool is the capability
  // itself. An enum with no `read` taking it is not a location, which is why a
  // bus cycle kind cannot be one.
  //
  // `read_memory` is excluded by name; an overload taking more than the location
  // is excluded by arity.
  [[nodiscard]] static consteval std::vector<std::meta::info> location_scopes() {
    std::vector<std::meta::info> scopes;
    for (const auto member: std::meta::members_of(^^Machine, std::meta::access_context::current())) {
      if (!std::meta::is_function(member) || !std::meta::has_identifier(member))
        continue;
      if (std::meta::identifier_of(member) != read_verb)
        continue;
      const auto parameters = std::meta::parameters_of(member);
      if (parameters.size() != 1)
        continue;
      if (const auto type = std::meta::type_of(parameters[0]);
          std::meta::is_enum_type(type) && !std::ranges::contains(scopes, type))
        scopes.push_back(type);
    }
    return scopes;
  }

  // Where a *value* may come from, for a vocabulary that names its scope: any
  // enum an operation takes. Derived the same way and for the same reason, from
  // what the CPU can be asked to do rather than from anything it declares about
  // itself.
  [[nodiscard]] static consteval std::vector<std::meta::info> named_scopes() {
    auto scopes = location_scopes();
    for (const auto candidate: operations())
      for (const auto parameter: std::meta::parameters_of(candidate))
        if (const auto type = std::meta::type_of(parameter);
            std::meta::is_enum_type(type) && !std::ranges::contains(scopes, type))
          scopes.push_back(type);
    return scopes;
  }

  // Whether a declaration carries `[[=refract::operation]]`. The annotation's
  // type is const-qualified when it came from the constant, so the qualifier is
  // taken off before comparing.
  [[nodiscard]] static consteval bool is_operation(const std::meta::info fn) {
    for (const auto annotation: std::meta::annotations_of(fn))
      if (std::meta::remove_cv(std::meta::type_of(annotation)) == ^^Operation)
        return true;
    return false;
  }

  // Every function a description may name: the machine's marked members,
  // static or not, and every public static function of each palette.
  // `has_identifier` excludes the implicitly-declared special members, which
  // have no name to compare.
  [[nodiscard]] static consteval std::vector<std::meta::info> operations() {
    std::vector<std::meta::info> found;
    for (const auto member: std::meta::members_of(^^Machine, std::meta::access_context::current()))
      if (std::meta::is_function(member) && std::meta::has_identifier(member) && is_operation(member))
        found.push_back(member);
    for (const auto palette: Target::palettes())
      for (const auto member: std::meta::members_of(palette, std::meta::access_context::current()))
        if (std::meta::is_function(member) && std::meta::is_static_member(member) && std::meta::has_identifier(member))
          found.push_back(member);
    return found;
  }

  // Names in a description are matched the way assembler is written, without regard to case.
  [[nodiscard]] static consteval char to_lower_case(const char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }

  // Every location name means exactly one thing, checked over the whole pool
  // rather than as each name happens to be looked up. `only_match` would catch an
  // ambiguity, but only for a name some description writes; this makes it a
  // property of the machine, so a CPU that grows a second `carry` is told at once
  // rather than whenever a row first wants one.
  [[nodiscard]] static consteval bool location_names_are_unique() {
    std::vector<std::pair<std::string, std::meta::info>> seen;
    for (const auto scope: location_scopes())
      for (const auto enumerator: std::meta::enumerators_of(scope)) {
        auto name = std::string(spelling_of(enumerator));
        std::ranges::transform(name, name.begin(), to_lower_case);
        if (const auto earlier = std::ranges::find(seen, name, &std::pair<std::string, std::meta::info>::first);
            earlier != seen.end())
          throw std::runtime_error("two of this machine's readable locations are spelled '" + name + "' (in " +
                                   std::string(std::meta::identifier_of(earlier->second)) + " and " +
                                   std::string(std::meta::identifier_of(scope)) +
                                   "), so a description could not say which it meant");
        seen.emplace_back(name, scope);
      }
    return true;
  }

  [[nodiscard]] static consteval bool same_ignoring_case(const std::string_view lhs, const std::string_view rhs) {
    return std::ranges::equal(lhs, rhs, {}, to_lower_case, to_lower_case);
  }

  // Every name a table uses must resolve to exactly one thing. Throwing from a
  // `consteval` function is how a bad name becomes a compile error naming the
  // line of the description that wrote it.
  [[nodiscard]] static consteval std::meta::info only_match(
      const std::span<const std::meta::info> candidates, const std::string_view name, const std::size_t line) {
    if (candidates.empty())
      throw error(line, "this CPU has nothing named '" + std::string(name) + "'");
    if (candidates.size() > 1) {
      // Naming both is the point: the scopes are found by scanning rather than
      // listed, so "more than one" is most likely a scope the reader did not know
      // was being searched.
      std::string found;
      for (const auto candidate: candidates)
        found +=
            (found.empty() ? " (in " : ", ") + std::string(std::meta::identifier_of(std::meta::parent_of(candidate)));
      throw error(line, "this CPU has more than one thing named '" + std::string(name) + "'" + found + ")");
    }
    return candidates.front();
  }

  // What a description calls one enumerator: the `Spelling` it declares, or its
  // own identifier when it declares none. An annotation's type is
  // const-qualified, so the qualifier comes off before the comparison.
  [[nodiscard]] static consteval std::string spelling_of(const std::meta::info enumerator) {
    for (const auto annotation: std::meta::annotations_of(enumerator))
      if (std::meta::remove_cv(std::meta::type_of(annotation)) == ^^Spelling)
        return std::string(std::meta::extract<Spelling>(annotation).text.view());
    return std::string(std::meta::identifier_of(enumerator));
  }

  // The scope a vocabulary named. Compared exactly: it is a C++ type's name, not
  // something written the way assembly is written.
  [[nodiscard]] static consteval std::meta::info find_scope(const std::string_view name, const std::size_t line) {
    std::string offered;
    for (const auto scope: named_scopes()) {
      if (std::meta::identifier_of(scope) == name)
        return scope;
      offered += (offered.empty() ? " (this CPU offers " : ", ") + std::string(std::meta::identifier_of(scope));
    }
    throw error(line, "no scope named '" + std::string(name) + "'" + offered + ")");
  }

  // An enumerator in one of the scopes the CPU offers, such as the Z80's `a`,
  // `hl`, `carry` and `pc`. A vocabulary that named a scope searches that one and
  // no other; everything else searches them all.
  [[nodiscard]] static consteval std::meta::info find_location(
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
    // A spelling is consulted only when no identifier matched: almost every
    // name is an identifier, and reading every enumerator's annotations on
    // every lookup was measured to cost more than it is worth.
    if (candidates.empty())
      for (const auto everywhere: location_scopes())
        for (const auto enumerator: std::meta::enumerators_of(everywhere))
          if (same_ignoring_case(spelling_of(enumerator), name))
            candidates.push_back(enumerator);
    return only_match(candidates, name, line);
  }

  // An enumerator of the enum a *parameter* asks for. The parameter type is the
  // scope, and that is what keeps these names out of the location namespace: a
  // spelling is free to collide with the name of a register and mean something
  // else entirely. (On the Z80, `i` and `d` are a direction here and the I and D
  // registers everywhere else.)
  [[nodiscard]] static consteval std::meta::info find_spelling(
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
      throw error(line, "no member of '" + std::string(std::meta::identifier_of(scope)) + "' is called '" +
                            std::string(name) + "'" + (offered.empty() ? ", which has no members" : offered + ")"));
    if (candidates.size() > 1)
      throw error(line, "more than one member of '" + std::string(std::meta::identifier_of(scope)) + "' is called '" +
                            std::string(name) + "'");
    return candidates.front();
  }

  // The locations a view selects between, in the order its vocabulary lists them,
  // so that the view *is* the index. Every member resolves to a location of the
  // same type, which `check_view_vocabulary` guarantees, so the machine is handed
  // a location it already knows how to read and needs no notion of a view.
  //
  // `template for` rather than a loop, because a splice needs a constant operand
  // and an expansion statement's induction variable is one.
  template<Resolved Op, std::size_t Line>
  [[nodiscard]] static consteval auto locations_of_view() {
    constexpr const auto &vocabulary = Compiled::vocabularies()[Op.view_vocabulary];
    constexpr const auto &members = vocabulary.members;
    // The scope comes from the vocabulary rather than from a member, because a
    // member is parsed before anything knows which vocabulary it will end up in.
    constexpr auto scope = vocabulary.scope;
    constexpr auto first = find_location(members[0].operand.name.view(), Line, scope);
    template for (constexpr auto at: std::views::iota(1uz, members.size())) {
      if (std::meta::type_of(find_location(members[at].operand.name.view(), Line, scope)) != std::meta::type_of(first))
        throw error(Line, "'" + std::string(members[at].operand.name.view()) + "' and '" +
                              std::string(members[0].operand.name.view()) +
                              "' are different kinds of location, and a view selects among one kind");
    }
    using Location = [:std::meta::type_of(first):];
    std::array<Location, members.size()> locations{};
    template for (constexpr auto at: std::views::iota(0uz, members.size())) {
      locations[at] = [:find_location(members[at].operand.name.view(), Line, scope):];
    }
    return locations;
  }

  // The operation a row names, such as the Z80's `inc8`, `add16` and `is_set`.
  [[nodiscard]] static consteval std::meta::info find_operation(const std::string_view name, const std::size_t line) {
    std::vector<std::meta::info> candidates;
    for (const auto candidate: operations())
      if (same_ignoring_case(std::meta::identifier_of(candidate), name))
        candidates.push_back(candidate);
    // A marked member the scan above could not see is a mistake worth its own
    // message: access control would otherwise decide, in silence, that it is
    // not an operation.
    if (candidates.empty())
      for (const auto member: std::meta::members_of(^^Machine, std::meta::access_context::unchecked()))
        if (std::meta::is_function(member) && std::meta::has_identifier(member) && is_operation(member) &&
            same_ignoring_case(std::meta::identifier_of(member), name))
          throw error(line, "'" + std::string(name) +
                                "' is marked as an operation but is not public, so a "
                                "description cannot reach it");
    return only_match(candidates, name, line);
  }

  // How many parameters `Fn` declares. A variable template so that the count
  // reads without parentheses, and is one spelling in `if constexpr`,
  // `static_assert` and diagnostics alike.
  template<std::meta::info Fn>
  static constexpr std::size_t arity_of = std::meta::parameters_of(Fn).size();

  // The type of `Fn`'s `I`th parameter.
  template<std::meta::info Fn, std::size_t I>
  using parameter_type = [:std::meta::type_of(std::meta::parameters_of(Fn)[I]):];

  // The members a result is split across, or an empty span for a result that is
  // one value. A class comes apart when every non-static data member is public
  // and its own; one that hides all of its state is one value. One that hides
  // some of it, or has a base class, is refused, because a row could be given
  // only part of it. Stricter than a structured binding, which also accepts
  // members all in one base.
  [[nodiscard]] static consteval std::span<const std::meta::info> decomposes_into(
      const std::meta::info type, const std::size_t line) {
    if (!std::meta::is_class_type(type))
      return {};
    const auto visible = std::meta::nonstatic_data_members_of(type, std::meta::access_context::current());
    const auto every = std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked());
    if (visible.size() != every.size()) {
      if (!visible.empty())
        throw error(line, "this operation returns a type with some of its members hidden, so a row could be "
                          "given only part of it");
      return {};
    }
    if (!std::meta::bases_of(type, std::meta::access_context::unchecked()).empty())
      throw error(line, "this operation returns a type with a base class, whose members a row could not be given");
    return std::define_static_array(visible);
  }

  // Whether `Fn` is a member of the machine, called on it, rather than a static
  // function called on nothing. A member reaches the machine as `this`, which
  // is not a parameter, so the row's operands are the whole parameter list
  // either way; one that only reads the machine is `const`. `^^Machine`
  // reflects the alias, and a parent is never an alias, hence `dealias`.
  template<std::meta::info Fn>
  static constexpr bool machine_member =
      (std::meta::parent_of(Fn) == std::meta::dealias(^^Machine)) && !std::meta::is_static_member(Fn);

  // Whether `Fn` asks for the machine as a parameter, which nothing may: an
  // operation that needs the machine is a member of it. One asking this way
  // would be handed a row's operand instead, and the error would be about that
  // operand rather than about the signature, so it is named here.
  template<std::meta::info Fn>
  static constexpr bool asks_for_machine = [] {
    if constexpr (arity_of<Fn> == 0)
      return false;
    else
      return std::is_same_v<std::remove_cvref_t<parameter_type<Fn, 0>>, Machine>;
  }();

  // What the instruction carries: the immediate its encoding fetched, the view a
  // prefix chose, and the opcode itself. Fixed for the whole of one instruction.
  // Passed by value, and it must be: a handler ends in a mandatory tail call,
  // which abandons the frame, so nothing in the frame may have had its address
  // taken.
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
    // The description line, for diagnostics. Threaded through `value_of` and
    // `store` as its own template parameter and kept out of `Resolved`:
    // `Resolved` is a template argument, and equal operands on different lines
    // would become different arguments, splitting every instantiation below for
    // a field only an error message reads.
    std::size_t line{};
    constexpr bool operator==(const Call &) const = default;
  };

  // The rule the paragraph above states, said in a way the compiler checks. Give
  // `Resolved` a `std::string_view` and this fires here, rather than as a
  // deduction failure several hundred lines away from the cause.
  static_assert(std::meta::is_structural_type(^^Name));
  static_assert(std::meta::is_structural_type(^^Resolved));
  static_assert(std::meta::is_structural_type(^^Call));

  // `as_resolved` copies `Operand` into `Resolved` field by field, so a field
  // added to either would arrive default-constructed with nothing said, which is
  // a wrong emulator rather than a compile error. These are the counts it was
  // written against. The check is here rather than beside it because `Model.hpp`
  // is plain data and reflects on nothing; if one of these fires, read
  // `as_resolved` and `resolve` and decide which of them owns the new field.
  static_assert(std::meta::nonstatic_data_members_of(^^Operand, std::meta::access_context::current()).size() == 9,
      "Operand has gained or lost a field; `as_resolved` may no longer copy all of it");
  static_assert(std::meta::nonstatic_data_members_of(^^Resolved, std::meta::access_context::current()).size() == 13,
      "Resolved has gained or lost a field; `as_resolved` and `resolve` may no longer fill all of it");

  // An operand becomes the type the parameter it feeds asks for. A constant is
  // checked here, because the table wrote it and a value too big for its
  // parameter is a mistake worth naming. A location converts the ordinary way, so
  // whether a 16-bit register reaching an 8-bit parameter is diagnosed depends on
  // the build's warnings rather than on anything this file does.
  template<Resolved Op, std::size_t Line, typename Parameter>
  [[nodiscard]] static Parameter direct_value_of(Machine &machine, const Decoded decoded) {
    static_assert(!std::is_reference_v<Parameter>,
        "an operation takes its operands by value; there is nothing here for a reference to bind to");
    if constexpr (Op.kind == Resolved::Kind::Constant) {
      // An enum parameter has names for its values, and a spelling annotation
      // exists to expose them, so a description must name one rather than cast
      // a number into it. That holds for a number read from the opcode as much
      // as one written in the row.
      static_assert(!std::is_enum_v<Parameter>,
          "this parameter is an enum, so name one of its spellings rather than passing a number");
      if constexpr (Op.from_opcode)
        // The instruction carries the number and the slice says where. Nothing to
        // check against the parameter: the mask already bounds it.
        return static_cast<Parameter>(Op.slice.extract(decoded.opcode));
      else {
        if constexpr (std::integral<Parameter>)
          static_assert(Op.constant <= static_cast<std::uintmax_t>(std::numeric_limits<Parameter>::max()),
              "this constant does not fit the parameter it is passed to");
        return static_cast<Parameter>(Op.constant);
      }
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
      return machine.read(locations[decoded.view]);
    }
    else if constexpr (std::is_enum_v<Parameter>)
      // The name is one of the enum's members rather than a place to read from,
      // so it is spliced as a value and nothing is fetched. Whether a name is
      // read or handed over as an enumerator is decided by the parameter's type,
      // not by the row. The enum is the one the vocabulary named, or the
      // parameter's type for an operand no vocabulary owns, such as one a member
      // appends.
      return [:find_spelling(
                   Op.scope.empty() ? ^^Parameter : find_scope(Op.scope.view(), Line), Op.name.view(), Line):];
    else
      // An *enumerator* splice: this yields a prvalue whose type is the enum the
      // name was found in, so the machine's overload set decides what reading it
      // means: on the Z80, `machine.read(R8::A)` and `machine.read(Flags::Bit::carry)` are
      // different functions returning different types, chosen here by overload
      // resolution.
      return machine.read([:find_location(Op.name.view(), Line, Op.scope.view()):]);
  }

  // The address an indirect operand addresses through. A displaced one was formed
  // once for the whole instruction, before any operand was touched.
  template<Resolved Op, std::size_t Line>
  [[nodiscard]] static std::uint16_t address_of(Machine &machine, const Decoded decoded, const std::uint16_t indexed) {
    if constexpr (Op.displaced)
      return indexed;
    else
      return direct_value_of<Op, Line, std::uint16_t>(machine, decoded);
  }

  // An indirect operand is whatever it would have been, read as an address. How
  // wide the read is comes from the parameter it feeds rather than from anything
  // the row says, so one operand spelling serves every width the machine offers.
  // (On the Z80 that is `ld16 hl <- (n)` reading two bytes where `ld8 a <- (n)`
  // reads one.)
  template<Resolved Op, std::size_t Line, typename Parameter>
  [[nodiscard]] static Parameter value_of(Machine &machine, const Decoded decoded, const std::uint16_t indexed) {
    if constexpr (Op.indirect) {
      // The machine offers two widths and the parameter's type picks. Said out
      // loud because the alternative is an `else` that quietly means "one byte":
      // an operation declaring `unsigned` rather than `std::uint16_t` would read
      // half of what it asked for and zero-extend the rest.
      static_assert(std::same_as<Parameter, std::uint8_t> || std::same_as<Parameter, std::uint16_t>,
          "an indirect operand is read at one of the two widths the machine offers");
      const auto address = address_of<Op, Line>(machine, decoded, indexed);
      if constexpr (std::same_as<Parameter, std::uint16_t>)
        return machine.read_memory16(address);
      else
        return machine.read_memory(address);
    }
    else
      return direct_value_of<Op, Line, Parameter>(machine, decoded);
  }

  // The counterpart of `value_of`: a value goes to an operand, written through
  // an address if the operand is indirect and into the location it names
  // otherwise. A `-` destination drops it.
  template<Resolved Op, std::size_t Line, typename T>
  static void store(Machine &machine, const Decoded decoded, const std::uint16_t indexed, const T value) {
    if constexpr (Op.kind == Resolved::Kind::Discard)
      static_cast<void>(value);
    else if constexpr (Op.indirect) {
      static_assert(std::same_as<T, std::uint8_t> || std::same_as<T, std::uint16_t>,
          "an indirect destination is written at one of the two widths the machine offers");
      // The addressing mode says how long the machine idles before writing back.
      if constexpr (Op.write_back_delay != 0)
        machine.delay(Op.write_back_delay);
      const auto address = address_of<Op, Line>(machine, decoded, indexed);
      if constexpr (std::same_as<T, std::uint16_t>)
        machine.write_memory16(address, value);
      else
        machine.write_memory(address, value);
    }
    else {
      static_assert(Op.kind == Resolved::Kind::Named, "only a named location can be a destination");
      if constexpr (Op.from_view) {
        static constexpr auto locations = locations_of_view<Op, Line>();
        machine.write(locations[decoded.view], value);
      }
      else
        machine.write([:find_location(Op.name.view(), Line, Op.scope.view()):], value);
    }
  }

  // Which of the row's operands feeds each parameter: by position, unless the
  // row wrote `value=…`, in which case by the parameter's declared name. Naming
  // exists because position is a silent coupling: `test_bit(value, bit, flags,
  // bus)` takes three `std::uint8_t`s, and a row could swap two and still
  // compile. Naming is all or nothing within a step, so there is no rule about
  // what "the next one" means.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static consteval std::array<std::size_t, C.operands.size()> operand_for_parameter() {
    std::array<std::size_t, C.operands.size()> written{};
    std::size_t named = 0;
    for (const auto &operand: C.operands)
      if (!operand.parameter.empty())
        ++named;
    if (named == 0) {
      std::ranges::iota(written, 0uz);
      return written;
    }
    if (named != C.operands.size())
      throw error(C.line, "this step names some of its parameters and not others; name all of them or none, so that "
                          "reading it needs no "
                          "rule about which is which");

    const auto parameters = std::meta::parameters_of(Fn);
    std::string offered;
    for (std::size_t slot = 0; slot < written.size(); ++slot) {
      const auto parameter = parameters[slot];
      if (!std::meta::has_identifier(parameter))
        throw error(C.line, "this operation was declared without parameter names, so there is nothing to name here");
      offered += (offered.empty() ? " (it takes " : ", ") + std::string(std::meta::identifier_of(parameter));
    }
    for (std::size_t slot = 0; slot < written.size(); ++slot) {
      const auto name = std::meta::identifier_of(parameters[slot]);
      std::size_t found = 0;
      std::size_t matches = 0;
      for (const auto [at, operand]: std::views::enumerate(C.operands))
        if (same_ignoring_case(operand.parameter.view(), name)) {
          found = static_cast<std::size_t>(at);
          ++matches;
        }
      if (matches == 0)
        throw error(C.line, "no operand is given for '" + std::string(name) + "'" + offered + ")");
      if (matches > 1)
        throw error(C.line, "'" + std::string(name) + "' is given more than one operand");
      written[slot] = found;
    }
    return written;
  }

  // The inverse: which parameter each operand, in the order the row wrote it,
  // ends up feeding. Needed because an operand is *read* where the row put it and
  // *passed* where the signature wants it, and its type comes from the latter.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static consteval std::array<std::size_t, C.operands.size()> parameter_for_operand() {
    constexpr auto operand = operand_for_parameter<Fn, C>();
    std::array<std::size_t, C.operands.size()> parameter{};
    for (std::size_t slot = 0; slot < operand.size(); ++slot)
      parameter[operand[slot]] = slot;
    return parameter;
  }

  // The row's operands, each converted to the type of the parameter it feeds,
  // evaluated in the order the row wrote them. The order matters: resolving one
  // can read memory and move the address bus, and a row may then ask what that
  // read left there. A call's arguments are evaluated in an unspecified order,
  // so the values are materialised into a braced tuple first, which is sequenced
  // left to right ([dcl.init.list]/4). Naming a parameter changes which argument
  // an operand becomes, never when it is read.
  //
  // A pack rather than `template for`: an expansion statement produces
  // statements, and an argument list needs a pack.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static auto operands_of(Machine &machine, const Decoded decoded, const std::uint16_t indexed) {
    constexpr auto parameter = parameter_for_operand<Fn, C>();
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple{
          value_of<C.operands[I], C.line, parameter_type<Fn, parameter[I]>>(machine, decoded, indexed)...};
    }(std::make_index_sequence<C.operands.size()>{});
  }

  // Where the row's order and the signature's order are reconciled: the tuple
  // holds the values in the order they were read, and this hands them over in the
  // order the parameters want them.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static auto call_with(Machine &machine, const auto &arguments) {
    constexpr auto operand = operand_for_parameter<Fn, C>();
    return [&]<std::size_t... S>(std::index_sequence<S...>) {
      if constexpr (machine_member<Fn>)
        return machine.[:Fn:](std::get<operand[S]>(arguments)...);
      else
        return [:Fn:](std::get<operand[S]>(arguments)...);
    }(std::make_index_sequence<C.operands.size()>{});
  }

  // One member of a returned struct. A splice in member-access position is
  // legitimate and reads as a typo, so it appears once, here, rather than inline
  // at the only place that wants it.
  template<std::meta::info Member>
  [[nodiscard]] static constexpr decltype(auto) member_of_result(const auto &result) {
    return result.[:Member:];
  }

  // An operation's name as a diagnostic quotes it.
  [[nodiscard]] static consteval std::string quoted_name_of(const std::meta::info fn) {
    return "'" + std::string(std::meta::identifier_of(fn)) + "'";
  }

  // The three checks on a step's shape follow. Each throws from `consteval`, so
  // a step that does not fit its operation is a compile error naming the
  // description line and the operation, as every other mistake in a description
  // is reported.
  //
  // The row supplies every parameter the operation has.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static consteval bool operands_fit() {
    if (asks_for_machine<Fn>)
      throw error(C.line, quoted_name_of(Fn) + " takes the machine as a parameter; an operation that needs the "
                                               "machine is a member of it, marked [[=refract::operation]]");
    if (C.operands.size() != arity_of<Fn>)
      throw error(C.line, quoted_name_of(Fn) + " takes " + decimal(arity_of<Fn>) +
                              " operand(s) and this row supplies " + decimal(C.operands.size()));
    return true;
  }

  // What the operation returns decides how many destinations the row names:
  // none for `void`, one or more for a single value, and exactly one per part for
  // a result that comes apart. A result with one part is refused rather than
  // guessed at: it describes one value as well as it describes a bundle holding
  // one, and a row would be written differently depending on which was meant.
  template<std::meta::info Fn, Call C, typename Result>
  [[nodiscard]] static consteval bool destinations_fit(const std::span<const std::meta::info> parts) {
    const auto name = quoted_name_of(Fn);
    const auto destinations = C.destinations.size();
    if (parts.size() == 1)
      throw error(C.line, name + " returns a type with one part, which could mean one value or a bundle holding "
                                 "one; give it a second part, or keep its state private so it is one value");
    if constexpr (std::is_void_v<Result>) {
      if (destinations != 0)
        throw error(C.line, name + " returns nothing, so this row may not name a destination");
    }
    else if (parts.size() > 1) {
      if (destinations != parts.size())
        throw error(C.line, name + " returns " + decimal(parts.size()) + " parts and this row names " +
                                decimal(destinations) + " destination(s)");
    }
    else if (destinations == 0)
      throw error(C.line, name + " returns a value, so this row must name a destination");
    return true;
  }

  // A condition tests only what the row hands it, so that the row says
  // everything the branch depends on. That rules out a member of the machine,
  // even a `const` one, since a machine in hand can be asked anything. It
  // names no destination, and answers yes or no.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static consteval bool condition_fits() {
    const auto name = quoted_name_of(Fn);
    if (machine_member<Fn> || asks_for_machine<Fn>)
      throw error(C.line, name + " reaches the machine; a condition tests only what the row hands it, so that "
                                 "the row states everything the branch depends on");
    if (C.operands.size() != arity_of<Fn>)
      throw error(C.line, name + " takes " + decimal(arity_of<Fn>) + " operand(s) and this condition supplies " +
                              decimal(C.operands.size()));
    if (!C.destinations.empty())
      throw error(C.line, name + " is a condition, which decides whether the rest of the row happens and names "
                                 "no destination");
    if (std::meta::return_type_of(Fn) != ^^bool)
      throw error(C.line, name + " is used as a condition, so it must return bool");
    return true;
  }

  // Arguments are supplied positionally, or by name where the row said so;
  // destinations destructure the result in declaration order.
  template<std::meta::info Fn, Call C>
  static void apply(Machine &machine, const Decoded decoded, const std::uint16_t indexed) {
    // Gating the body on the arity, rather than only asserting it, keeps a wrong
    // count from being one message followed by twenty: the `decltype` below
    // instantiates a parameter type per operand, and an operand with no
    // parameter would index past the end of the parameter list.
    constexpr bool arity_matches = C.operands.size() == arity_of<Fn>;
    static_assert(operands_fit<Fn, C>());
    if constexpr (arity_matches) {
      const auto call = [&machine](const auto &arguments) { return call_with<Fn, C>(machine, arguments); };

      // Unevaluated, despite everything just said about operands having effects:
      // `decltype` asks for the type and calls nothing.
      using Result = decltype(call(operands_of<Fn, C>(machine, decoded, indexed)));
      // A `std::span`, and safe to hold: `decomposes_into` promotes its contents
      // with `define_static_array`, so what this points at has static storage and
      // `members[at]` is a constant expression a splice can use.
      static constexpr auto members = decomposes_into(^^Result, C.line);
      static_assert(destinations_fit<Fn, C, Result>(members));
      if constexpr (std::is_void_v<Result>) {
        call(operands_of<Fn, C>(machine, decoded, indexed));
      }
      else if constexpr (members.size() > 1) {
        // Two is a value and the flags it set, which is what almost every
        // arithmetic operation returns.
        const auto result = call(operands_of<Fn, C>(machine, decoded, indexed));
        template for (constexpr auto at: std::views::iota(0uz, C.destinations.size())) {
          store<C.destinations[at], C.line>(machine, decoded, indexed, member_of_result<members[at]>(result));
        }
      }
      else {
        // More than one *destination* is how an instruction writes one result to
        // two places, as the Z80's `dd cb d op` puts it through the addressing mode
        // and into the register its low bits name.
        const auto result = call(operands_of<Fn, C>(machine, decoded, indexed));
        template for (constexpr auto destination: C.destinations)
            store<destination, C.line>(machine, decoded, indexed, result);
      }
    }
  }

  // A condition is applied like any other operation; only what is done with the
  // answer differs.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static bool evaluate(Machine &machine, const Decoded decoded, const std::uint16_t indexed) {
    static_assert(condition_fits<Fn, C>());
    // Gated for the same reason `apply` is: a condition that does not fit gets
    // one message rather than that message and the cascade from calling it.
    if constexpr (!machine_member<Fn> && C.operands.size() == arity_of<Fn>)
      return call_with<Fn, C>(machine, operands_of<Fn, C>(machine, decoded, indexed));
    else
      return false;
  }

  // A vocabulary member may bind the operation late, and may append an operand the
  // encoding does not carry.
  [[nodiscard]] static constexpr Member member_for(
      const Step &step, const Pattern &matched, const std::uint8_t opcode, const Rules &rules) {
    if (!step.operation_reference)
      return {};
    return member_of({.vocabularies = Compiled::vocabularies(), .matched = matched, .rules = rules, .opcode = opcode},
        *step.operation_reference);
  }

  // Where a step becomes a `Call`: its operands and destinations resolved
  // against this opcode, then whatever the vocabulary member appends.
  [[nodiscard]] static constexpr Call call_for(
      const Step &step, const Pattern &matched, const std::uint8_t opcode, const std::size_t line, const Rules &rules) {
    const auto member = member_for(step, matched, opcode, rules);
    const Resolution at{.vocabularies = Compiled::vocabularies(), .matched = matched, .rules = rules, .opcode = opcode};
    Call result{.line = line};
    for (const auto &operand: step.operands)
      if (!result.operands.try_push_back(resolve(at, operand)))
        throw table_error(Compiled::file, line, "too many operands");
    for (const auto &written: step.destinations) {
      auto destination = resolve(at, written);
      // The idle cycle belongs to a write-back, so only to something read through
      // the same address it will be written through.
      const auto was_read = std::ranges::any_of(
          result.operands, [&](const Resolved &operand) { return same_address(operand, destination); });
      if (!was_read)
        destination.write_back_delay = 0;
      if (!result.destinations.try_push_back(destination))
        throw table_error(Compiled::file, line, "too many destinations");
    }
    // A vocabulary member may append an operand the encoding does not carry. It
    // named no vocabulary, so it is already resolved and has no scope: which enum
    // a name means here is the parameter's business.
    for (const auto &argument: member.arguments)
      if (!result.operands.try_push_back(as_resolved(argument)))
        throw table_error(Compiled::file, line, "too many operands");
    return result;
  }

  // The signature of every generated handler. One signature, because a table of
  // function pointers has one, so `latch` and `view` ride on every handler even
  // where no prefix can reach it; both are arguments because a prefix chooses
  // them and the row's own bytes do not carry them. A handler does not report
  // where to go next: a `goto` row tail-calls the next table's handler, so a run
  // of prefix bytes costs a fetch a byte rather than a frame a byte.
  //
  // Positional rather than bundled into a struct, because bundling was measured
  // to cost more through a function pointer (notes/MEASUREMENTS.md).
  using Handler = void (*)(Machine &, std::uint8_t latch, std::uint8_t view, std::uint8_t opcode);

  // One row, fully unrolled: every step spliced in, in order, with nothing of the
  // table surviving into the generated code. There is one of these per *body*, a
  // row together with the slices it reads, so every opcode of a row that reads
  // none of its variable bits shares one; `body_key` is what decides. Each is
  // typically a handful of instructions, because every choice below is made at
  // compile time.
  //
  // `Table` and `BodyKey` are template parameters rather than arguments precisely
  // so that `Compiled::rows()[Index]`, the vocabulary lookups, and the renaming rules
  // are all constants here. `BodyKey` is not the opcode: it is the opcode with
  // every slice this body does not read cleared, so the compile-time lookups
  // below see only the bits that vary the code they generate, while the run-time
  // `opcode` parameter still carries all of them. See `body_key`.
  template<std::uint8_t Table, std::uint8_t BodyKey, std::size_t Index>
  static void execute_one(
      Machine &machine, const std::uint8_t latch, const std::uint8_t view, const std::uint8_t opcode) {
    // A reference into the compiled arrays, which have static storage, so the
    // expansion statement below can walk `row.steps` as a range: a range's
    // *address* has to be a constant, and a local copy's would not be.
    static constexpr auto row = Compiled::rows()[Index];
    // A renaming applies to every row this table decodes, inherited or its own. A
    // rule names the vocabulary it rewrites, not just the member, so a row can opt
    // out of a renaming by naming a vocabulary no rule mentions. (That is how the
    // Z80's `ld {real:y}, {index_mem:view}` keeps a real h.)
    static constexpr auto rules = Compiled::tables()[Table].rules;
    // The base this body is displaced through, if any. `static` because the
    // lambda below uses it without capturing it, so it has to have static
    // storage.
    static constexpr auto displaced = displaced_through(Compiled::vocabularies(), row, BodyKey, rules);
    constexpr bool entered_latched = Compiled::latched()[Table];
    // The displacement byte comes before any immediate, unless the table was
    // entered with one already latched, in which case it arrived before this
    // opcode did.
    const std::uint8_t displacement =
        row.reads_displacement || (displaced && !entered_latched) ? machine.fetch_immediate() : latch;
    // A `goto` is the whole of its row: a prefix reads no operands and has no
    // immediate, so nothing below this line applies to one. It is also why the
    // hand-over happens *here* rather than among the steps: a mandatory tail
    // call abandons the frame, so it is refused wherever a local has had its
    // address taken, and the lambda that forms `indexed` takes several.
    if constexpr (row.steps.size() == 1 && row.steps[0].kind == Step::Kind::Goto) {
      constexpr auto step = row.steps[0];
      constexpr std::uint8_t next_table = step.target;
      const auto next_view = static_cast<std::uint8_t>(step.forwards_view ? view : step.target_view);
      // The next table's opcode is fetched here, since the hand-over is the loop.
      // A latched table's opcode arrives as an operand read rather than an
      // instruction fetch: the machine has already committed, so it costs less
      // and does not refresh.
      const auto next_opcode = Compiled::latched()[next_table] ? machine.fetch_immediate() : machine.fetch_opcode();
      [[gnu::musttail]] return dispatch<next_table>[next_opcode](machine, displacement, next_view, next_opcode);
    }
    else {

      // The encoding column says what is fetched, and it is fetched once, before
      // any step, rather than where it is used: the order arguments are
      // evaluated in is unspecified, so a fetch inside a call could land after
      // a memory access the row puts before it.
      const std::uint16_t immediate = [&machine] -> std::uint16_t {
        if constexpr (row.immediate_bytes == 2)
          return machine.fetch_immediate16();
        else if constexpr (row.immediate_bytes == 1)
          return machine.fetch_immediate();
        else {
          static_assert(row.immediate_bytes == 0, "the parser allows at most two immediate bytes");
          return 0;
        }
      }();
      // Formed once, after both, and handed to every operand that shares it. The
      // machine is told what else was read first, because on a Z80 those reads
      // happen *inside* the window that forms the address rather than before it.
      const Decoded decoded{.immediate = immediate, .view = view, .opcode = opcode};
      const std::uint16_t indexed = [&machine, decoded, displacement] -> std::uint16_t {
        if constexpr (displaced) {
          // A latched table read its opcode inside the same window, so that byte
          // counts too, and the machine is charged for the window once rather
          // than for each read inside it.
          //
          // The count is a template argument so that the machine, which knows how
          // long its window is, can refuse a count it cannot hold at compile time.
          constexpr std::uint8_t read_inside = row.immediate_bytes + (entered_latched ? 1 : 0);
          return machine.template displaced_address<read_inside>(
              direct_value_of<*displaced, row.line, std::uint16_t>(machine, decoded), displacement);
        }
        else
          return 0;
      }();
      // Expanded, not looped: the body is instantiated once per step, and `step` is
      // `constexpr` inside it, which is what lets its contents be template
      // arguments. A `return` here leaves `execute_one`, not the expansion.
      template for (constexpr auto step: row.steps) {
        {
          constexpr auto member = member_for(step, row.matched, BodyKey, rules);
          constexpr auto verb = step.operation_reference ? member.operation : step.operation;
          constexpr auto call = call_for(step, row.matched, BodyKey, row.line, rules);
          if constexpr (step.kind == Step::Kind::If) {
            // The rest of the row is the conditional half, which is where the
            // extra cycles of a taken branch come from too. `break` rather than
            // `return`, because abandoning the rest of a row is not abandoning
            // the run: the hand-over below still has to happen, and a `return`
            // here stops the machine at the first untaken branch.
            if (!evaluate<find_operation(verb, row.line), call>(machine, decoded, indexed))
              break;
          }
          else
            apply<find_operation(verb, row.line), call>(machine, decoded, indexed);
        }
      }
    }
    // The row is done, so hand on to the next instruction rather than returning:
    // this hand-over is the run loop.
    [[gnu::musttail]] return continue_running(machine, 0, 0, 0);
  }

  // ---------------------------------------------------------------------------
  // One function per instruction
  // ---------------------------------------------------------------------------
  //
  // A row's distinct bodies are the combinations of the slices it *reads*: a
  // catch-all reading none is one function for every opcode it claims, and a row
  // reading two three-bit slices is sixty-four. The functions below enumerate
  // those bodies once each and fill a table's 256 entries by pointing at them.

  // Which of a row's slices change the generated code. Three kinds do not: a
  // view is a run-time value, a numeric vocabulary is read straight out of the
  // opcode, and the mnemonic's own references are the disassembler's business;
  // nothing below this line ever looks at `row.pieces`.
  [[nodiscard]] static constexpr Vector<std::uint8_t, Pattern::max_slices> slices_read_by(const Row &row) {
    Vector<std::uint8_t, Pattern::max_slices> used;
    const auto note = [&used](const Reference reference) {
      if (reference.from_view || is_numeric(Compiled::vocabularies()[reference.vocabulary_index]))
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
  // this opcode wants: two opcodes of one row share a body exactly when this
  // agrees. The slices `slices_read_by` leaves out must be exactly the ones the
  // generated code does not branch on; a new kind of reference the code
  // branches on has to be noted there, or two opcodes would share a body they
  // disagree about. TableTest checks that every opcode of every body agrees
  // with its key.
  [[nodiscard]] static constexpr std::uint8_t body_key(const Row &row, const std::uint8_t opcode) {
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

  [[nodiscard]] static consteval Decoding decoding_for(const std::uint8_t table) {
    Decoding result;
    // Indexed, never searched: `made[row][key]` is the body this row already has
    // for that combination of the slices it reads. Per row as well as per key,
    // because two rows may narrow to the same encoding and are still two rows.
    std::vector<std::array<std::optional<std::uint16_t>, 256>> made(Compiled::rows().size());
    for (const auto opcode: std::views::iota(0uz, 256uz)) {
      const auto row = *Compiled::find_row(table, static_cast<std::uint8_t>(opcode));
      const auto key = body_key(Compiled::rows()[row], static_cast<std::uint8_t>(opcode));
      auto &body = made[row][key];
      if (!body) {
        body = static_cast<std::uint16_t>(result.bodies.size());
        result.bodies.push_back({.row = row, .opcode = key});
      }
      result.fill[opcode] = *body;
    }
    return result;
  }

  // A table's bodies, and which body each of its 256 opcodes uses, as arrays:
  // `decoding_for` answers in a `std::vector`, which `to_array` fixes.
  template<std::uint8_t Table>
  static constexpr auto bodies_of = to_array<[] { return decoding_for(Table).bodies; }>();
  template<std::uint8_t Table>
  static constexpr auto fill_of = decoding_for(Table).fill;

  // A table's 256 handlers, one per opcode, pointing at the bodies `bodies_of`
  // enumerated. `execute_one` takes its row and encoding as template arguments,
  // which a `template for` can supply and a loop cannot; filling the entries
  // from the functions made is a loop, because by then they are values.
  template<std::uint8_t Table>
  static constexpr auto dispatch = [] {
    std::array<Handler, bodies_of<Table>.size()> made{};
    template for (constexpr auto at: std::views::iota(0uz, bodies_of<Table>.size())) {
      made[at] = &execute_one<Table, bodies_of<Table>[at].opcode, bodies_of<Table>[at].row>;
    }
    std::array<Handler, 256> handlers{};
    std::ranges::transform(fill_of<Table>, handlers.begin(), [&made](const std::uint16_t body) { return made[body]; });
    return handlers;
  }();

  // Where one instruction becomes the next. Every handler ends here, and this
  // ends in the next handler, so a run of instructions is a chain of tail calls
  // and the stack never grows. The machine decides whether there is a next one:
  // `start_instruction` is where a Z80 takes its interrupt and idles its halt,
  // none of which is the framework's business.
  //
  // Handler-shaped so that the tail call out of a handler is a tail call: the
  // three arguments a fresh instruction has no use for are passed as zero.
  static void continue_running(Machine &machine, std::uint8_t, std::uint8_t, std::uint8_t) {
    if (!machine.start_instruction())
      return;
    const auto opcode = machine.fetch_opcode();
    [[gnu::musttail]] return dispatch<Compiled::entry_table>[opcode](machine, 0, 0, opcode);
  }

  // Starts the run. The handlers tail-call each other from here on, so this is
  // the only frame the run keeps.
  //
  // The uniqueness check lives here rather than beside `MachineLike` at class
  // scope because a class-scope assertion cannot call a member function of the
  // class it is in; this is the one entry point, so it runs once regardless.
  static void run(Machine &machine) {
    static_assert(location_names_are_unique(), "two of this machine's readable locations are spelled the same, so a "
                                               "description could not say which it meant");
    // The handlers reach the description's parts directly, so this is where
    // building an interpreter checks it.
    static_assert(Compiled::check());
    continue_running(machine, 0, 0, 0);
  }
};

} // namespace specbolt::refract
