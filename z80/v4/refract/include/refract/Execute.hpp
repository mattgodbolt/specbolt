#pragma once

// Turns a compiled description into an interpreter for a machine. `Compiled` has already read the text and lowered it
// to `constexpr` data; nothing here parses anything. What is left is to look up the names that data holds in the
// machine, and to emit one function per body: a row together with the slices it reads, so that opcodes generating the
// same code share one.
//
// A target names the two, and the palettes its description may draw verbs from; `TargetLike` in Machine.hpp is the
// contract:
//
//   struct Target {
//     using Machine = ...;                       // see Machine.hpp
//     using Compiled = refract::Compiled<Source>;  // see Compiled.hpp
//     static consteval std::vector<std::meta::info> palettes();
//   };
//
// A palette is a type every public static function of which is a verb. The machine's own verbs are the members it
// publishes with `[[=refract::operation]]`, static or not, and its locations the enums its `[[=refract::location]]`
// `read` overloads take; see Model.hpp.
//
// `Interpreter<Target>::run` then runs the machine until it says stop.

#include "refract/Decode.hpp"
#include "refract/Machine.hpp"
#include "refract/Model.hpp"
#include "refract/TableError.hpp"
#include "refract/Visit.hpp"
#include "refract/Workarounds.hpp"

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
// **Reflection (P2996).** `^^X` yields a `std::meta::info`: one type that can denote *any* entity: a type, a function,
// an enumerator, a data member. That one-type-for-everything is why `find_location` and `find_operation` have the same
// shape despite looking for very different things. `info` is a structural type, so it can be a non-type template
// parameter, which is the hinge the whole file turns on: `template<std::meta::info Fn>` makes "which function" part of
// a template instantiation's identity.
//
// **Splices**, `[: … :]`, turn an `info` back into code. One syntax, which denotes whatever the reflection designated;
// the forms here are
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
//   result.[:Member:]                  a data member, in member-access position.
//
// **Expansion statements (P1306)**, `template for`. The body is *instantiated once per element*, so it is code size
// rather than a loop, and the induction variable is `constexpr` inside the body, which is what lets it be used as a
// template argument. A `return` inside one returns from the enclosing function, not from an iteration. There is
// deliberately no `template switch`: the body of an expansion statement is control-flow-limited, so a `case` label
// inside it can only belong to a `switch` that is also inside it, and a 256-way dispatch cannot be expanded into one.
// Hence a table of function pointers.
//
// **`consteval` functions that throw, called from `consteval {}` blocks.** Nothing swallows the exception: `naming` and
// `at_line` catch one only to rethrow it with the file and line in front. A block runs its statements during
// compilation, a throw that escapes one is not a constant expression, and *that* is the diagnostic: a mistake in the
// description becomes a compile error carrying its line number. This is the most surprising idiom in the file, and it
// is used everywhere a check has a line to report against.
//
// **`std::define_static_array`.** `nonstatic_data_members_of` returns a `std::vector`, whose allocation cannot survive
// constant evaluation. This promotes the contents into an object with static storage duration, so a `span` over it
// *can* escape into a `constexpr` variable and still be usable as a template argument afterwards.
//
// **`std::meta::access_context::current()`** means the context of the function that names it, `Interpreter`'s own scope
// here rather than the caller's, and `Interpreter` is nobody's friend. It is why a private helper in an operation scope
// cannot be named by a table.
//
// ---------------------------------------------------------------------------
// Why the data looks the way it does
// ---------------------------------------------------------------------------
//
// `Call` and `Resolved` are non-type template parameters, so they must be *structural*: literal types whose members and
// bases are all public, recursively. That single requirement explains a lot of the model: why `Vector` exposes its
// `storage` and `count`, and why `Name` is a fixed-size `std::array` of `char` rather than a `std::string_view` (which
// has private members and is not structural).
//
// It is also why the parse cannot simply hand its `std::vector`s over: `std::define_static_array` would promote them,
// but only for a structural element type, and a `Row` holds `std::string_view`s. `ToArray.hpp` is what stands in its
// place.

// The interpreter for `Target`: one generated handler per body of its description, and the run loop that chains them.
template<TargetLike Target>
struct Interpreter {
  using Machine = typename Target::Machine;
  using Compiled = typename Target::Compiled;

  // A mistake in the description, reported against its line and the file the target says it came from.
  [[nodiscard]] static consteval std::runtime_error error(const std::size_t line, const std::string_view what) {
    return table_error(Compiled::file, line, what);
  }

  // Whether a declaration carries an annotation of type `mark`, such as `[[=refract::operation]]`. The annotation's
  // type is const-qualified when it came from the constant, so the qualifier is taken off before comparing.
  [[nodiscard]] static consteval bool is_marked(const std::meta::info declaration, const std::meta::info mark) {
    return std::ranges::any_of(std::meta::annotations_of(declaration), [mark](const std::meta::info annotation) {
      return std::meta::remove_cv(std::meta::type_of(annotation)) == mark;
    });
  }

  // Scans the machine for the enums a location name may come from: the one each of its `[[=refract::location]]`
  // overloads takes. An enum no marked overload takes is not a location, however public a `read` of it may be; the
  // Z80's `Bus`, which only `Z80::bus` takes, is not one.
  //
  // The scan sees private members too, so that a mark on something the generated code could not call is an error rather
  // than silently ignored: a location is read and written by calling `read` and `write` on it, so the mark belongs only
  // on a public `read` taking the one enum.
  [[nodiscard]] static consteval std::vector<std::meta::info> scan_location_scopes() {
    std::vector<std::meta::info> scopes;
    for (const auto member: std::meta::members_of(^^Machine, std::meta::access_context::unchecked())) {
      if (!std::meta::is_function(member) || !is_marked(member, ^^Location))
        continue;
      const auto parameters = std::meta::parameters_of(member);
      if (!std::meta::is_public(member) || !std::meta::has_identifier(member) ||
          std::meta::identifier_of(member) != read_verb || parameters.size() != 1 ||
          !std::meta::is_enum_type(std::meta::type_of(parameters[0])))
        throw std::runtime_error(
            (std::meta::has_identifier(member) ? quoted_name_of(member) : std::string("a member")) +
            " is marked [[=refract::location]], so it must be a public `" + std::string(read_verb) +
            "` taking one enum, the location it reads");
      if (const auto type = std::meta::type_of(parameters[0]); !std::ranges::contains(scopes, type))
        scopes.push_back(type);
    }
    return scopes;
  }

  // Gathers the enums a vocabulary may name as its scope: those the machine's marked `read` overloads take, and any
  // enum an operation takes as a parameter. Nothing is declared a scope as such: an enum is one because a location or
  // an operation uses it.
  [[nodiscard]] static consteval std::vector<std::meta::info> scan_named_scopes() {
    std::vector<std::meta::info> scopes(location_scopes.begin(), location_scopes.end());
    for (const auto candidate: operations)
      for (const auto parameter: std::meta::parameters_of(candidate))
        if (const auto type = std::meta::type_of(parameter);
            std::meta::is_enum_type(type) && !std::ranges::contains(scopes, type))
          scopes.push_back(type);
    return scopes;
  }

  // Scans the machine and its palettes for every function a description may name: the machine's marked members, static
  // or not, and every public static function of each palette. A description names a function by its identifier, so
  // `has_identifier` leaves out what has none: operator and conversion functions, constructors and destructors. In a
  // palette, where `is_static_member` has already removed all but the operators, that means a static `operator()` or
  // `operator[]`, or a class's own `operator new` or `operator delete`.
  [[nodiscard]] static consteval std::vector<std::meta::info> scan_operations() {
    std::vector<std::meta::info> found;
    for (const auto member: std::meta::members_of(^^Machine, std::meta::access_context::current()))
      if (std::meta::is_function(member) && std::meta::has_identifier(member) && is_marked(member, ^^Operation))
        found.push_back(member);
    for (const auto palette: Target::palettes())
      for (const auto member: std::meta::members_of(palette, std::meta::access_context::current()))
        if (std::meta::is_function(member) && std::meta::is_static_member(member) && std::meta::has_identifier(member))
          found.push_back(member);
    return found;
  }

  // What the scans above find, worked out once per machine and promoted to static storage, which is what every lookup
  // reads. A scan reads every member of the machine with its annotations, and every member of each palette, while a
  // description looks a name up at every step it writes, so scanning at each lookup is where much of a compile went
  // (notes/FINDINGS.md, "Scanning once rather than at every lookup"). The type is spelled rather than deduced: deducing
  // it would need the initialiser while the class is still being instantiated, before the members the scans call.
  static constexpr std::span<const std::meta::info> location_scopes = std::define_static_array(scan_location_scopes());
  static constexpr std::span<const std::meta::info> operations = std::define_static_array(scan_operations());
  static constexpr std::span<const std::meta::info> named_scopes = std::define_static_array(scan_named_scopes());

  // `c` in lower case, if it is an ASCII letter. Names in a description are matched the way assembler is written,
  // without regard to case.
  [[nodiscard]] static consteval char to_lower_case(const char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }

  // Checks that no two of the machine's readable locations share a name, ignoring case, and throws naming both scopes
  // if two do. A location is claimed by every name `find_location` can match it by, its identifier and the spelling it
  // declares, so a spelling that collides with another location's identifier is caught too. Checked over the whole pool
  // rather than as each name happens to be looked up: `only_match` would catch an ambiguity, but only for a name some
  // description writes; this makes it a property of the machine, so a machine that grows a second `carry` is told at
  // once rather than whenever a row first wants one.
  static consteval void check_location_names_unique() {
    std::vector<std::pair<std::string, std::meta::info>> seen;
    const auto claim = [&seen](std::string name, const std::meta::info scope) {
      std::ranges::transform(name, name.begin(), to_lower_case);
      if (const auto earlier = std::ranges::find(seen, name, &std::pair<std::string, std::meta::info>::first);
          earlier != seen.end())
        throw std::runtime_error("two of this machine's readable locations are spelled '" + name + "' (in " +
                                 std::string(std::meta::identifier_of(earlier->second)) + " and " +
                                 std::string(std::meta::identifier_of(scope)) +
                                 "), so a description could not say which it meant");
      seen.emplace_back(name, scope);
    };
    for (const auto scope: location_scopes)
      for (const auto enumerator: std::meta::enumerators_of(scope)) {
        const auto identifier = std::meta::identifier_of(enumerator);
        claim(std::string(identifier), scope);
        // An enumerator with no spelling of its own is spelled by its identifier, which is already claimed.
        if (const auto spelling = spelling_of(enumerator); !same_ignoring_case(spelling, identifier))
          claim(spelling, scope);
      }
  }

  // Whether two names are the same once case is ignored.
  [[nodiscard]] static consteval bool same_ignoring_case(const std::string_view lhs, const std::string_view rhs) {
    return std::ranges::equal(lhs, rhs, {}, to_lower_case, to_lower_case);
  }

  // `names` separated by commas, as a diagnostic lists what it found or what was on offer.
  [[nodiscard]] static consteval std::string comma_separated(std::ranges::viewable_range auto &&names) {
    return std::forward<decltype(names)>(names) | std::views::join_with(std::string_view(", ")) |
           std::ranges::to<std::string>();
  }

  // The one candidate, when there is exactly one; an error against `line` otherwise. Every name a table uses must
  // resolve to exactly one thing, and throwing from a `consteval` function is how a bad name becomes a compile error
  // naming the line of the description that wrote it.
  [[nodiscard]] static consteval std::meta::info only_match(
      const std::span<const std::meta::info> candidates, const std::string_view name, const std::size_t line) {
    if (candidates.empty())
      throw error(line, "this CPU has nothing named '" + std::string(name) + "'");
    if (candidates.size() > 1) {
      // Naming both is the point: the scopes are found by scanning rather than listed, so "more than one" is most
      // likely a scope the reader did not know was being searched.
      // A scope's parent may be the global or an unnamed namespace, which has no identifier to show, so it is shown as
      // the compiler writes it.
      const auto scope_of = [](const std::meta::info candidate) {
        const auto parent = std::meta::parent_of(candidate);
        return std::meta::has_identifier(parent) ? std::meta::identifier_of(parent)
                                                 : std::meta::display_string_of(parent);
      };
      throw error(line, "this CPU has more than one thing named '" + std::string(name) + "' (in " +
                            comma_separated(candidates | std::views::transform(scope_of)) + ")");
    }
    return candidates.front();
  }

  // What a description calls one enumerator: the `Spelling` it declares, or its own identifier when it declares none.
  // An annotation's type is const-qualified, so the qualifier comes off before the comparison.
  [[nodiscard]] static consteval std::string spelling_of(const std::meta::info enumerator) {
    for (const auto annotation: std::meta::annotations_of(enumerator))
      if (std::meta::remove_cv(std::meta::type_of(annotation)) == ^^Spelling)
        return std::string(std::meta::extract<Spelling>(annotation).text.view());
    return std::string(std::meta::identifier_of(enumerator));
  }

  // The enum a vocabulary named as its scope, or an error listing what the machine offers. Compared exactly: it is a
  // C++ type's name, not something written the way assembly is written. Two enums of that name from different
  // namespaces are as ambiguous as any other name, so `only_match` reports them.
  [[nodiscard]] static consteval std::meta::info find_scope(const std::string_view name, const std::size_t line) {
    std::vector<std::meta::info> candidates;
    for (const auto scope: named_scopes)
      if (std::meta::identifier_of(scope) == name)
        candidates.push_back(scope);
    if (candidates.empty()) {
      const auto offered = named_scopes | std::views::transform([](const std::meta::info scope) {
        return std::meta::identifier_of(scope);
      });
      throw error(line, "no scope named '" + std::string(name) + "'" +
                            (named_scopes.empty() ? ", and this CPU offers none"
                                                  : " (this CPU offers " + comma_separated(offered) + ")"));
    }
    return only_match(candidates, name, line);
  }

  // The enumerator a location name denotes, in one of the scopes the CPU offers, such as the Z80's `a`, `hl`, `carry`
  // and `pc`. A vocabulary that named a scope searches that one and no other; everything else searches them all.
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
    for (const auto everywhere: location_scopes)
      for (const auto enumerator: std::meta::enumerators_of(everywhere))
        if (same_ignoring_case(std::meta::identifier_of(enumerator), name))
          candidates.push_back(enumerator);
    // Identifiers are searched first and spellings only if none matched. The order cannot change the answer, since
    // `check_location_names_unique` refuses a machine where a name is both; it is chosen because reading every
    // enumerator's annotations is the expensive half (notes/FINDINGS.md, "Compile time, and where it went when it
    // moved").
    if (candidates.empty())
      for (const auto everywhere: location_scopes)
        for (const auto enumerator: std::meta::enumerators_of(everywhere))
          if (same_ignoring_case(spelling_of(enumerator), name))
            candidates.push_back(enumerator);
    return only_match(candidates, name, line);
  }

  // The enumerator of `scope` that a description spells `name`, for an enum a *parameter* asks for. The parameter type
  // is the scope, and that is what keeps these names out of the location namespace: a spelling is free to collide with
  // the name of a register and mean something else entirely. (On the Z80, `i` and `d` are a direction here and the I
  // and D registers everywhere else.)
  [[nodiscard]] static consteval std::meta::info find_spelling(
      const std::meta::info scope, const std::string_view name, const std::size_t line) {
    const auto enumerators = std::meta::enumerators_of(scope);
    std::vector<std::meta::info> candidates;
    for (const auto enumerator: enumerators)
      if (same_ignoring_case(spelling_of(enumerator), name))
        candidates.push_back(enumerator);
    if (candidates.empty()) {
      const auto offered =
          enumerators | std::views::transform([](const std::meta::info enumerator) { return spelling_of(enumerator); });
      throw error(line,
          "no member of '" + std::string(std::meta::identifier_of(scope)) + "' is called '" + std::string(name) + "'" +
              (enumerators.empty() ? ", which has no members" : " (it has " + comma_separated(offered) + ")"));
    }
    if (candidates.size() > 1)
      throw error(line, "more than one member of '" + std::string(std::meta::identifier_of(scope)) + "' is called '" +
                            std::string(name) + "'");
    return candidates.front();
  }

  // The name of the location a member of a view's vocabulary stands for; an error against `line` for a member that is
  // not a named location.
  [[nodiscard]] static consteval std::string_view location_named(const Member &member, const std::size_t line) {
    const auto not_a_location = [&] {
      return error(line, "'" + std::string(member.display) +
                             "' is selected by a view, so it must name a location the machine can read");
    };
    return refract::visit(
        Overloaded{
            [&](const Operand &operand) {
              return refract::visit(Overloaded{
                                        [](const Operand::Named &named) { return named.name.view(); },
                                        [&](const OneOf<Operand::Constant, Operand::Immediate, Operand::Vocabulary,
                                            Operand::Discard> auto &) -> std::string_view { throw not_a_location(); },
                                    },
                  operand.kind);
            },
            [&](const OneOf<Member::Operation, Member::Hole> auto &) -> std::string_view { throw not_a_location(); },
        },
        member.kind);
  }

  // The locations a view selects between, in the order its vocabulary lists them, so that the view *is* the index.
  // Every member must name a location of the same type, which the first loop below checks (`check_view_vocabulary` has
  // only made them operands of one shape), so the machine is handed a location it already knows how to read and needs
  // no notion of a view.
  //
  // `template for` rather than a loop, because a splice needs a constant operand and an expansion statement's induction
  // variable is one.
  template<Resolved Op, std::size_t Line>
  [[nodiscard]] static consteval auto locations_of_view() {
    constexpr const auto &vocabulary = Compiled::vocabularies()[Op.view_vocabulary];
    constexpr const auto &members = vocabulary.members;
    // The scope comes from the vocabulary rather than from a member, because a member is parsed before anything knows
    // which vocabulary it will end up in.
    constexpr auto scope = vocabulary.scope;
    constexpr auto first = find_location(location_named(members[0], Line), Line, scope);
    template for (constexpr auto at: std::views::iota(1uz, members.size())) {
      if (std::meta::type_of(find_location(location_named(members[at], Line), Line, scope)) !=
          std::meta::type_of(first))
        throw error(Line, "'" + std::string(members[at].display) + "' and '" + std::string(members[0].display) +
                              "' are different kinds of location, and a view selects among one kind");
    }
    using Kind = [:std::meta::type_of(first):];
    std::array<Kind, members.size()> locations{};
    template for (constexpr auto at: std::views::iota(0uz, members.size())) {
      locations[at] = [:find_location(location_named(members[at], Line), Line, scope):];
    }
    return locations;
  }

  // The function an operation name denotes, such as the Z80's `inc8`, `add16` and `is_set`; an error against `line` if
  // there is none or more than one.
  [[nodiscard]] static consteval std::meta::info find_operation(const std::string_view name, const std::size_t line) {
    std::vector<std::meta::info> candidates;
    for (const auto candidate: operations)
      if (same_ignoring_case(std::meta::identifier_of(candidate), name))
        candidates.push_back(candidate);
    // A marked member the scan above could not see is a mistake worth its own message: access control would otherwise
    // decide, in silence, that it is not an operation.
    if (candidates.empty())
      for (const auto member: std::meta::members_of(^^Machine, std::meta::access_context::unchecked()))
        if (std::meta::is_function(member) && std::meta::has_identifier(member) && is_marked(member, ^^Operation) &&
            same_ignoring_case(std::meta::identifier_of(member), name))
          throw error(line, "'" + std::string(name) +
                                "' is marked as an operation but is not public, so a "
                                "description cannot reach it");
    return only_match(candidates, name, line);
  }

  // How many parameters `Fn` declares. A variable template so that the count reads without parentheses, and is one
  // spelling in `if constexpr`, checks and diagnostics alike.
  template<std::meta::info Fn>
  static constexpr std::size_t arity_of = std::meta::parameters_of(Fn).size();

  // The type of `Fn`'s `I`th parameter.
  template<std::meta::info Fn, std::size_t I>
  using parameter_type = [:std::meta::type_of(std::meta::parameters_of(Fn)[I]):];

  // The members a result is split across, or an empty span for a result that is one value. An aggregate, a bundle of
  // public fields, comes apart into its members; any other type is one value, as the Z80's `Flags` is. An aggregate
  // with a base class is refused, because a row could be given only part of it.
  [[nodiscard]] static consteval std::span<const std::meta::info> decomposes_into(
      const std::meta::info type, const std::size_t line) {
    if (!std::meta::is_class_type(type) || !std::meta::is_aggregate_type(type))
      return {};
    if (!std::meta::bases_of(type, std::meta::access_context::current()).empty())
      throw error(line, "this operation returns a type with a base class, whose members a row could not be given");
    return std::define_static_array(std::meta::nonstatic_data_members_of(type, std::meta::access_context::current()));
  }

  // Whether `Fn` is a member of the machine, called on it, rather than a static function called on nothing. Not being
  // static says so, because `operations` takes only static functions from a palette. A member reaches the machine as
  // `this`, which is not a parameter, so the row's operands are the whole parameter list either way; one that only
  // reads the machine is `const`.
  template<std::meta::info Fn>
  static constexpr bool machine_member = !std::meta::is_static_member(Fn);

  // Whether any parameter of `Fn` is the machine, which nothing may ask for: an operation that needs the machine is a
  // member of it. One asking this way would be handed a row's operand instead, and the error would be about that
  // operand rather than about the signature, so it is named here. Both sides are dealiased because `^^Machine`, and a
  // parameter's type, may reflect an alias.
  template<std::meta::info Fn>
  static constexpr bool asks_for_machine =
      std::ranges::any_of(std::meta::parameters_of(Fn), [](const std::meta::info parameter) {
        return std::meta::dealias(std::meta::remove_cvref(std::meta::type_of(parameter))) ==
               std::meta::dealias(^^Machine);
      });

  // What the instruction carries: the immediate its encoding fetched, the view a prefix chose, and the opcode itself.
  // Fixed for the whole of one instruction. Passed by value, and it must be: a handler ends in a mandatory tail call,
  // which abandons the frame, so nothing in the frame may have had its address taken.
  struct Decoded {
    std::uint16_t immediate{};
    std::uint8_t view{};
    std::uint8_t opcode{};
  };

  // Everything one step needs, with its vocabulary references already resolved. This is a non-type template parameter,
  // so every member of it, and of everything it contains, has to be public. See the note on structural types above.
  struct Call {
    Vector<Resolved, max_operands> operands{};
    Vector<Resolved, max_operands> destinations{};
    // The description line, for diagnostics. It belongs to the step rather than to any one operand, and `resolve`,
    // which makes each `Resolved`, has no line to give, so `value_of` and `store` take it as a template argument of its
    // own.
    std::size_t line{};
    constexpr bool operator==(const Call &) const = default;
  };

  // Checks that the types used as template arguments are structural, the rule set out under "Why the data looks the way
  // it does" at the top of this file, in one place rather than at whichever template first takes the type.
  static_assert(std::meta::is_structural_type(^^Name),
      "Name is a template argument, so every member must be public and itself structural");
  static_assert(std::meta::is_structural_type(^^Resolved),
      "Resolved is a template argument, so every member and base must be public and itself structural");
#if REFRACT_CLANG_WORKAROUNDS
  // Completes `Call` before it is asked about. See notes/WASM.md, "The fork on the host, first", item 1.
  static_assert(sizeof(Call) > 0);
#endif
  static_assert(std::meta::is_structural_type(^^Call),
      "Call is a template argument, so every member must be public and itself structural");

  // Reads one operand that is not an address and returns it as the type of the parameter it feeds: a constant or
  // immediate converted, an enumerator spliced, or a location read from the machine. Whether the operand suits the
  // parameter was settled by `check_operand_fits` before this is instantiated. A location converts the ordinary way, so
  // whether a 16-bit register reaching an 8-bit parameter is diagnosed depends on the build's warnings rather than on
  // anything here.
  template<Resolved Op, std::size_t Line, typename Parameter>
  [[nodiscard]] static Parameter direct_value_of(Machine &machine, const Decoded decoded) {
    if constexpr (Op.kind == Resolved::Kind::Constant) {
      if constexpr (Op.from_opcode) {
        // The instruction carries the number and the slice says where.
#if REFRACT_CLANG_WORKAROUNDS
        // Copied out before the call. See notes/WASM.md, "The fork on the host, first", item 6.
        constexpr auto slice = Op.slice;
        return static_cast<Parameter>(slice.extract(decoded.opcode));
#else
        return static_cast<Parameter>(Op.slice.extract(decoded.opcode));
#endif
      }
      else
        return static_cast<Parameter>(Op.constant);
    }
    else if constexpr (Op.kind == Resolved::Kind::Immediate) {
      if constexpr (Op.width == 1)
        return static_cast<std::uint8_t>(decoded.immediate);
      else
        return decoded.immediate;
    }
    else if constexpr (Op.from_view) {
      // Which member is not known until the table's view has been chosen, so the choice is an array index rather than a
      // splice. See `locations_of_view`.
      static constexpr auto locations = locations_of_view<Op, Line>();
      return machine.read(locations[decoded.view]);
    }
    else if constexpr (std::is_enum_v<Parameter>)
      // The name is one of the enum's members rather than a place to read from, so it is spliced as a value and nothing
      // is fetched. Whether a name is read or handed over as an enumerator is decided by the parameter's type, not by
      // the row. The enum is the one the vocabulary named, or the parameter's type for an operand no vocabulary owns,
      // such as one a member appends.
      return [:find_spelling(
                   Op.scope.empty() ? ^^Parameter : find_scope(Op.scope.view(), Line), Op.name.view(), Line):];
    else
      // An *enumerator* splice: this yields a prvalue whose type is the enum the name was found in, so the machine's
      // overload set decides what reading it means: on the Z80, `machine.read(R8::A)` and
      // `machine.read(Flags::Bit::carry)` are different functions returning different types, chosen here by overload
      // resolution.
      return machine.read([:find_location(Op.name.view(), Line, Op.scope.view()):]);
  }

  // The address an indirect operand addresses through. A displaced one was formed once for the whole instruction,
  // before any operand was touched.
  template<Resolved Op, std::size_t Line>
  [[nodiscard]] static std::uint16_t address_of(
      Machine &machine, const Decoded decoded, const std::uint16_t displaced_address) {
    if constexpr (Op.displaced)
      return displaced_address;
    else
      return direct_value_of<Op, Line, std::uint16_t>(machine, decoded);
  }

  // Reads one operand and returns it as the type of the parameter it feeds. An indirect operand is whatever it would
  // have been, read as an address; how wide the read is comes from the parameter rather than from anything the row
  // says, so one operand spelling serves every width the machine offers. (On the Z80 that is `ld16 hl <- (n)` reading
  // two bytes where `ld8 a <- (n)` reads one.)
  template<Resolved Op, std::size_t Line, typename Parameter>
  [[nodiscard]] static Parameter value_of(
      Machine &machine, const Decoded decoded, const std::uint16_t displaced_address) {
    if constexpr (Op.indirect) {
      const auto address = address_of<Op, Line>(machine, decoded, displaced_address);
      if constexpr (std::same_as<Parameter, std::uint16_t>)
        return machine.read_memory16(address);
      else
        return machine.read_memory(address);
    }
    else
      return direct_value_of<Op, Line, Parameter>(machine, decoded);
  }

  // Writes a value to one destination: through an address if the operand is indirect, into the location it names
  // otherwise, and nowhere for `-`. Whether the value suits the destination was settled by `check_destinations_fit`.
  template<Resolved Op, std::size_t Line, typename T>
  static void store(Machine &machine, const Decoded decoded, const std::uint16_t displaced_address, const T value) {
    if constexpr (Op.kind == Resolved::Kind::Discard)
      static_cast<void>(value);
    else if constexpr (Op.indirect) {
      // The addressing mode says how long the machine idles before writing back.
      if constexpr (Op.write_back_delay != 0)
        machine.delay(Op.write_back_delay);
      const auto address = address_of<Op, Line>(machine, decoded, displaced_address);
      if constexpr (std::same_as<T, std::uint16_t>)
        machine.write_memory16(address, value);
      else
        machine.write_memory(address, value);
    }
    else {
      if constexpr (Op.from_view) {
        static constexpr auto locations = locations_of_view<Op, Line>();
        machine.write(locations[decoded.view], value);
      }
      else
        machine.write([:find_location(Op.name.view(), Line, Op.scope.view()):], value);
    }
  }

  // For each parameter of `Fn`, in signature order, the index into `C.operands` of the operand that feeds it: by
  // position, unless the row wrote `value=…`, in which case by the parameter's declared name. Naming exists because
  // position is a silent coupling: an operation taking several parameters of one type, as the Z80's `test_bit(value,
  // bit, flags, bus)` does, lets a row swap two and still compile. Naming is all or nothing within a step, so there is
  // no rule about what "the next one" means.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static consteval std::array<std::size_t, C.operands.size()> operand_for_parameter() {
    std::array<std::size_t, C.operands.size()> written{};
    const auto is_named = [](const Resolved &operand) { return !operand.parameter.empty(); };
    if (std::ranges::none_of(C.operands, is_named)) {
      std::ranges::iota(written, 0uz);
      return written;
    }
    if (!std::ranges::all_of(C.operands, is_named))
      throw error(C.line, "this step names some of its parameters and not others; name all of them or none, so that "
                          "reading it needs no rule about which is which");

    const auto parameters = std::meta::parameters_of(Fn);
    const auto has_name = [](const std::meta::info parameter) { return std::meta::has_identifier(parameter); };
    if (!std::ranges::all_of(parameters, has_name))
      throw error(C.line, "this operation was declared without parameter names, so there is nothing to name here");
    for (const auto slot: std::views::iota(0uz, written.size())) {
      const auto name = std::meta::identifier_of(parameters[slot]);
      const auto feeds = [name](const Resolved &operand) { return same_ignoring_case(operand.parameter.view(), name); };
      const auto matches = std::ranges::count_if(C.operands, feeds);
      if (matches == 0) {
        const auto offered = parameters | std::views::transform([](const std::meta::info parameter) {
          return std::meta::identifier_of(parameter);
        });
        throw error(
            C.line, "no operand is given for '" + std::string(name) + "' (it takes " + comma_separated(offered) + ")");
      }
      if (matches > 1)
        throw error(C.line, "'" + std::string(name) + "' is given more than one operand");
      written[slot] = static_cast<std::size_t>(std::ranges::find_if(C.operands, feeds) - C.operands.begin());
    }
    return written;
  }

  // The inverse: for each operand, in the order the row wrote it, the index of the parameter of `Fn` it ends up
  // feeding. Needed because an operand is *read* where the row put it and *passed* where the signature wants it, and
  // its type comes from the latter.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static consteval std::array<std::size_t, C.operands.size()> parameter_for_operand() {
    constexpr auto operand = operand_for_parameter<Fn, C>();
    std::array<std::size_t, C.operands.size()> parameter{};
    for (const auto slot: std::views::iota(0uz, operand.size()))
      parameter[operand[slot]] = slot;
    return parameter;
  }

  // The row's operands, each converted to the type of the parameter it feeds, evaluated in the order the row wrote
  // them. The order matters: resolving one can read memory and move the address bus, and a row may then ask what that
  // read left there. A call's arguments are evaluated in an unspecified order, so the values are materialised into a
  // braced tuple first, which is sequenced left to right ([dcl.init.list]/4). Naming a parameter changes which argument
  // an operand becomes, never when it is read.
  //
  // A pack rather than `template for`: an expansion statement produces statements, and an argument list needs a pack.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static auto operands_of(
      Machine &machine, const Decoded decoded, const std::uint16_t displaced_address) {
    constexpr auto parameter = parameter_for_operand<Fn, C>();
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return std::tuple{
          value_of<C.operands[I], C.line, parameter_type<Fn, parameter[I]>>(machine, decoded, displaced_address)...};
    }(std::make_index_sequence<C.operands.size()>{});
  }

  // Calls `Fn` with the values in `arguments` and returns whatever it returns. This is where the row's order and the
  // signature's order are reconciled: the tuple holds the values in the order they were read, and this hands them over
  // in the order the parameters want them.
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

  // An operation's name as a diagnostic quotes it.
  [[nodiscard]] static consteval std::string quoted_name_of(const std::meta::info fn) {
    return "'" + std::string(std::meta::identifier_of(fn)) + "'";
  }

  // One of its parameters, likewise, or its position when it has no name.
  template<std::meta::info Fn>
  [[nodiscard]] static consteval std::string quoted_parameter_of(const std::size_t at) {
    const auto parameter = std::meta::parameters_of(Fn)[at];
    if (std::meta::has_identifier(parameter))
      return "'" + std::string(std::meta::identifier_of(parameter)) + "'";
    return "parameter " + decimal(at + 1);
  }

  // The checks on a step's shape follow. Each throws from `consteval`, so a step that does not fit its operation is a
  // compile error naming the description line and the operation, as every other mistake in a description is reported.

  // Checks one operand against the parameter it feeds: the parameter takes it by value, a number is not passed to an
  // enum, a constant the row wrote fits, and an address is read at a width the machine has.
  template<std::meta::info Fn, Call C, std::size_t I>
  static consteval void check_operand_fits() {
    constexpr auto operand = C.operands[I];
    constexpr auto at = parameter_for_operand<Fn, C>()[I];
    using Parameter = parameter_type<Fn, at>;
    const auto name = quoted_name_of(Fn);
    const auto parameter = quoted_parameter_of<Fn>(at);
    if (std::is_reference_v<Parameter>)
      throw error(C.line, name + " takes " + parameter + " by reference; an operation takes its operands by value");
    if constexpr (operand.kind == Resolved::Kind::Constant) {
      // An enum parameter has names for its values, and a spelling annotation exists to expose them, so a description
      // must name one rather than cast a number into it. That holds for a number read from the opcode as much as one
      // written in the row.
      if (std::is_enum_v<Parameter>)
        throw error(C.line, name + " takes " + parameter +
                                " as an enum, so name one of its spellings rather than "
                                "passing a number");
      // A number from the opcode is bounded by its slice; one the row wrote is bounded by nothing but this.
      if constexpr (std::integral<Parameter>)
        if (!operand.from_opcode &&
            operand.constant > static_cast<std::uintmax_t>(std::numeric_limits<Parameter>::max()))
          throw error(C.line, "constant " + decimal(operand.constant) + " does not fit " + parameter + " of " + name);
    }
    // The machine reads at two widths and the parameter's type picks between them, so any other type is refused rather
    // than quietly read as one byte: an operation declaring `unsigned` rather than `std::uint16_t` would otherwise get
    // half of what it asked for, zero-extended.
    if (operand.indirect && !std::same_as<Parameter, std::uint8_t> && !std::same_as<Parameter, std::uint16_t>)
      throw error(C.line, name + " reads " + parameter +
                              " through an address, so it must be std::uint8_t or "
                              "std::uint16_t, the widths the machine reads at");
  }

  // Checks every operand against the parameter it feeds.
  template<std::meta::info Fn, Call C>
  static consteval void check_each_operand_fits() {
    template for (constexpr auto at: std::views::iota(0uz, C.operands.size())) { check_operand_fits<Fn, C, at>(); }
  }

  // Checks a step against the operation it applies: the row supplies every parameter the operation has, and each
  // operand suits its parameter.
  template<std::meta::info Fn, Call C>
  static consteval void check_operands_fit() {
    if (asks_for_machine<Fn>)
      throw error(C.line, quoted_name_of(Fn) + " takes the machine as a parameter; an operation that needs the "
                                               "machine is a member of it, marked [[=refract::operation]]");
    if (C.operands.size() != arity_of<Fn>)
      throw error(C.line, quoted_name_of(Fn) + " takes " + decimal(arity_of<Fn>) +
                              " operand(s) and this row supplies " + decimal(C.operands.size()));
    check_each_operand_fits<Fn, C>();
  }

  // Checks a step's destinations against what the operation returns. The result decides how many the row names: none
  // for `void`, one or more for a single value, and exactly one per part for a result that comes apart. A result with
  // one part is refused rather than guessed at: it describes one value as well as it describes a bundle holding one,
  // and a row would be written differently depending on which was meant. Then each destination must be able to take
  // what it is handed.
  template<std::meta::info Fn, Call C, typename Result>
  static consteval void check_destinations_fit(const std::span<const std::meta::info> parts) {
    const auto name = quoted_name_of(Fn);
    const auto destinations = C.destinations.size();
    if (parts.size() == 1)
      throw error(C.line, name + " returns a type with one part, which could mean one value or a bundle holding "
                                 "one; give it a second part, or a constructor so it is one value");
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
    // A `-` drops anything, a location takes whatever the machine can write there, and an address takes only the widths
    // the machine writes at. The widths are spelled through aliases of our own because a reflect-expression may not
    // name a using-declarator, which is how a standard library may bring `uint8_t` into `std`.
#if REFRACT_CLANG_WORKAROUNDS
    // Marked because a reflect-expression is not counted as a use. See notes/WASM.md, "The fork on the host, first",
    // item 4.
    using Byte [[maybe_unused]] = std::uint8_t;
    using Word [[maybe_unused]] = std::uint16_t;
#else
    using Byte = std::uint8_t;
    using Word = std::uint16_t;
#endif
    for (const auto at: std::views::iota(0uz, C.destinations.size())) {
      const auto &destination = C.destinations[at];
      if (destination.kind == Resolved::Kind::Discard)
        continue;
      const auto handed =
          std::meta::dealias(std::meta::remove_cv(parts.size() > 1 ? std::meta::type_of(parts[at]) : ^^Result));
      if (destination.indirect) {
        if (handed != std::meta::dealias(^^Byte) && handed != std::meta::dealias(^^Word))
          throw error(C.line, name + " returns " + std::meta::display_string_of(handed) +
                                  ", which cannot be written through an address; the machine writes std::uint8_t "
                                  "or std::uint16_t");
      }
      else if (destination.kind != Resolved::Kind::Named)
        throw error(C.line, "destination " + decimal(at + 1) + " of " + name +
                                " is not a location; a result goes to a named location, an address or '-'");
    }
  }

  // Checks a condition, a step whose operation returns `Continue`, against that operation: it reaches no machine, is
  // supplied every parameter, and names no destination. A condition tests only what the row hands it, so that the row
  // says everything the branch depends on; that rules out a member of the machine, even a `const` one, since a machine
  // in hand can be asked anything.
  template<std::meta::info Fn, Call C>
  static consteval void check_condition_fits() {
    const auto name = quoted_name_of(Fn);
    if (machine_member<Fn> || asks_for_machine<Fn>)
      throw error(C.line, name + " reaches the machine; a condition tests only what the row hands it, so that "
                                 "the row states everything the branch depends on");
    if (C.operands.size() != arity_of<Fn>)
      throw error(C.line, name + " takes " + decimal(arity_of<Fn>) + " operand(s) and this condition supplies " +
                              decimal(C.operands.size()));
    if (!C.destinations.empty())
      throw error(C.line, name + " returns Continue, so it decides whether the rest of the row runs and has nowhere "
                                 "to write");
    check_each_operand_fits<Fn, C>();
  }

  // Runs one step: reads its operands, calls the operation, and stores what it returns in the step's destinations.
  // Arguments are supplied positionally, or by name where the row said so; destinations destructure the result in
  // declaration order.
  template<std::meta::info Fn, Call C>
  static void apply(Machine &machine, const Decoded decoded, const std::uint16_t displaced_address) {
    consteval { check_operands_fit<Fn, C>(); }
    using Result = [:std::meta::remove_cvref(std::meta::return_type_of(Fn)):];
    // A `std::span`, and safe to hold: `decomposes_into` promotes its contents with `define_static_array`, so what this
    // points at has static storage and `members[at]` is a constant expression a splice can use.
    static constexpr auto members = decomposes_into(^^Result, C.line);
    consteval { check_destinations_fit<Fn, C, Result>(members); }
    // Gated on the arity as well as checked, so that a wrong count is one message rather than one followed by a
    // cascade: reading the operands instantiates a parameter type per operand, and an operand with no parameter would
    // index past the end of the parameter list.
    if constexpr (C.operands.size() != arity_of<Fn>)
      return;
    else if constexpr (std::is_void_v<Result>)
      call_with<Fn, C>(machine, operands_of<Fn, C>(machine, decoded, displaced_address));
    else if constexpr (members.size() > 1) {
      // A result that comes apart is typically a value and the flags it set, one destination per part, each taken out
      // of the result by splicing in its member.
      const auto result = call_with<Fn, C>(machine, operands_of<Fn, C>(machine, decoded, displaced_address));
      template for (constexpr auto at: std::views::iota(0uz, C.destinations.size())) {
        store<C.destinations[at], C.line>(machine, decoded, displaced_address, result.[:members[at]:]);
      }
    }
    else {
      // More than one *destination* is how an instruction writes one result to two places, as the Z80's `dd cb d op`
      // puts it through the addressing mode and into the register its low bits name.
      const auto result = call_with<Fn, C>(machine, operands_of<Fn, C>(machine, decoded, displaced_address));
      template for (constexpr auto destination: C.destinations) {
        store<destination, C.line>(machine, decoded, displaced_address, result);
      }
    }
  }

  // Whether a function returns `Continue`, which is what makes it a condition. Decided from the signature, because
  // nothing in a row says so.
  [[nodiscard]] static consteval bool returns_continue(const std::meta::info fn) {
    return std::meta::dealias(std::meta::remove_cvref(std::meta::return_type_of(fn))) == ^^Continue;
  }

  // Checks that the operations of each vocabulary agree about being conditions. A row that names one by reference is
  // a condition at every opcode or at none, since nothing in the row can say which; a vocabulary that mixed the two
  // would make one row branch at some opcodes and run straight on at others.
  static consteval void check_conditions_agree() {
    for (const auto &vocabulary: Compiled::vocabularies()) {
      std::optional<std::pair<std::string_view, bool>> first;
      for (const auto &member: vocabulary.members)
        refract::visit(Overloaded{
                           [&](const Member::Operation &bound) {
                             const auto condition = returns_continue(find_operation(bound.name, vocabulary.line));
                             if (!first)
                               first = std::pair{bound.name, condition};
                             else if (first->second != condition)
                               throw error(vocabulary.line,
                                   "vocabulary '" + std::string(vocabulary.name) +
                                       "' mixes conditions with operations that are "
                                       "not ('" +
                                       std::string(condition ? bound.name : first->first) + "' returns Continue and '" +
                                       std::string(condition ? first->first : bound.name) +
                                       "' does not), so a row naming it "
                                       "would branch at some opcodes and run straight on at others");
                           },
                           [](const OneOf<Operand, Member::Hole> auto &) {},
                       },
            member.kind);
    }
  }

  // Runs one condition and returns its answer. A condition is applied like any other operation; only what is done with
  // the answer differs.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static Continue evaluate(
      Machine &machine, const Decoded decoded, const std::uint16_t displaced_address) {
    consteval { check_condition_fits<Fn, C>(); }
    // Gated for the same reason `apply` is: a condition that does not fit gets one message rather than that message and
    // the cascade from calling it.
    if constexpr (!machine_member<Fn> && C.operands.size() == arity_of<Fn>)
      return call_with<Fn, C>(machine, operands_of<Fn, C>(machine, decoded, displaced_address));
    else
      return Continue::no;
  }

  // Runs one step and says whether the row goes on: a condition's answer, or `Continue::yes` from any other step once
  // it has done its work.
  template<std::meta::info Fn, Call C>
  [[nodiscard]] static Continue run_step(
      Machine &machine, const Decoded decoded, const std::uint16_t displaced_address) {
    if constexpr (returns_continue(Fn))
      return evaluate<Fn, C>(machine, decoded, displaced_address);
    else {
      apply<Fn, C>(machine, decoded, displaced_address);
      return Continue::yes;
    }
  }

  // The operation a step applies at this opcode: the one it wrote by name, or the one its `{...}` reference selects,
  // which may bind some arguments the encoding does not carry. A reference that selects something else is an error,
  // which the caller puts a line in front of.
  [[nodiscard]] static constexpr Member::Operation operation_for(
      const Step &step, const Pattern &matched, const std::uint8_t opcode, const Rules &rules) {
    if (!step.operation_reference)
      return {.name = step.operation};
    const auto member =
        member_of({.vocabularies = Compiled::vocabularies(), .matched = matched, .rules = rules, .opcode = opcode},
            *step.operation_reference);
    return refract::visit(Overloaded{
                              [](const Member::Operation &bound) { return bound; },
                              [&](const Operand &) -> Member::Operation {
                                throw std::runtime_error("'" + std::string(member.display) +
                                                         "' is an operand, and this step names it where an operation "
                                                         "belongs");
                              },
                              [](const Member::Hole &) -> Member::Operation {
                                throw std::logic_error("a hole never decodes, so no step applies one");
                              },
                          },
        member.kind);
  }

  // The name of the operation a step applies at this opcode, as `operation_for` finds it, reported against the row's
  // file and line if it cannot be found.
  [[nodiscard]] static constexpr std::string_view verb_for(
      const Step &step, const Pattern &matched, const std::uint8_t opcode, const std::size_t line, const Rules &rules) {
    return naming(Compiled::file,
        [&] { return at_line(line, [&] { return operation_for(step, matched, opcode, rules).name; }); });
  }

  // The `Call` a step becomes: its operands and destinations resolved against this opcode, then whatever the vocabulary
  // member appends. Anything that goes wrong is reported against the row's file and line.
  [[nodiscard]] static constexpr Call call_for(
      const Step &step, const Pattern &matched, const std::uint8_t opcode, const std::size_t line, const Rules &rules) {
    return naming(Compiled::file, [&] {
      return at_line(line, [&] {
        const auto applied = operation_for(step, matched, opcode, rules);
        const Resolution at{
            .vocabularies = Compiled::vocabularies(), .matched = matched, .rules = rules, .opcode = opcode};
        Call result{.line = line};
        for (const auto &operand: step.operands)
          result.operands.push_back(resolve(at, operand));
        for (const auto &written: step.destinations) {
          auto destination = resolve(at, written);
          // The idle cycle belongs to a write-back, so only to something read through the same address it will be
          // written through.
          const auto was_read = std::ranges::any_of(
              result.operands, [&](const Resolved &operand) { return same_address(operand, destination); });
          if (!was_read)
            destination.write_back_delay = 0;
          result.destinations.push_back(destination);
        }
        // A vocabulary member may append an operand the encoding does not carry. It named no vocabulary, so it is
        // already resolved and has no scope: which enum a name means here is the parameter's business.
        for (const auto &argument: applied.arguments)
          result.operands.push_back(as_resolved(argument));
        return result;
      });
    });
  }

  // The signature of every generated handler. One signature, because a table of function pointers has one, so `latch`
  // and `view` ride on every handler even where no prefix can reach it; both are arguments because a prefix chooses
  // them and the row's own bytes do not carry them. A handler does not report where to go next: a `goto` row tail-calls
  // the next table's handler, so a run of prefix bytes costs a fetch a byte rather than a frame a byte.
  //
  // Positional rather than bundled into a struct, because bundling was measured to cost more through a function pointer
  // (notes/MEASUREMENTS.md).
  using Handler = void (*)(Machine &, std::uint8_t latch, std::uint8_t view, std::uint8_t opcode);

  // One row, fully unrolled: every step spliced in, in order, with nothing of the table surviving into the generated
  // code. There is one of these per *body*, a row together with the slices it reads, so every opcode of a row that
  // reads none of its variable bits shares one; `body_key` is what decides. Every choice below is made at compile
  // time, so what survives is the row's own work.
  //
  // `Table`, `BodyKey` and `Index` are template parameters rather than arguments precisely so that
  // `Compiled::rows()[Index]`, the vocabulary lookups, and the renaming rules are all constants here. `BodyKey` is not
  // the opcode: it is the opcode with every slice this body does not read cleared, so the compile-time lookups below
  // see only the bits that vary the code they generate, while the run-time `opcode` parameter still carries all of
  // them. See `body_key`.
  template<std::uint8_t Table, std::uint8_t BodyKey, std::size_t Index>
  static void execute_one(
      Machine &machine, const std::uint8_t latch, const std::uint8_t view, const std::uint8_t opcode) {
    static constexpr auto row = Compiled::rows()[Index];
    // A renaming applies to every row this table decodes, inherited or its own. A rule names the vocabulary it
    // rewrites, not just the member, so a row can opt out of a renaming by naming a vocabulary no rule mentions. (That
    // is how the Z80's `ld {real:y}, {index_mem:view}` keeps a real h.)
    static constexpr auto rules = Compiled::tables()[Table].rules;
    // The base this body is displaced through, if any. `static` because the displacement below tests it at run time,
    // through a member of `std::optional` that takes its address, and nothing in the frame a mandatory tail call
    // abandons may have had its address taken.
    static constexpr auto displaced = displaced_through(Compiled::vocabularies(), row, BodyKey, rules);
    constexpr bool entered_latched = Compiled::latched()[Table];
    // The displacement byte comes before any immediate, unless the table was entered with one already latched, in which
    // case it arrived before this opcode did.
    const std::uint8_t displacement =
        (row.reads_displacement || (displaced && !entered_latched)) ? machine.fetch_immediate() : latch;
    // A transfer is the whole of its row: a prefix reads no operands and has no immediate, so nothing in the steps'
    // branch applies to one. It is also why the hand-over happens *here* rather than among the steps: a mandatory tail
    // call abandons the frame, so it is refused wherever a local has had its address taken, and the steps' branch has
    // more locals to take one of. The chain ends in a `static_assert` so that a new kind of action is a compile error
    // here rather than a row that silently does nothing.
    if constexpr (std::holds_alternative<Transfer>(row.action)) {
      constexpr auto transfer = std::get<Transfer>(row.action);
      constexpr std::uint8_t next_table = transfer.target;
      const auto next_view = static_cast<std::uint8_t>(transfer.forwards_view ? view : transfer.target_view);
      // The next table's opcode is fetched here, since the hand-over is the loop. A latched table's opcode arrives by
      // `fetch_immediate` rather than `fetch_opcode`: the machine has already committed to an instruction, so this byte
      // is part of it rather than a choice of what to run. What that saves is the machine's business (on the Z80, a
      // cycle and a refresh).
      const auto next_opcode = Compiled::latched()[next_table] ? machine.fetch_immediate() : machine.fetch_opcode();
      // A mandatory tail call, in the one spelling both compilers accept (notes/WASM.md, "The fork on the host, first",
      // item 2).
      [[clang::musttail]] return dispatch<next_table>[next_opcode](machine, displacement, next_view, next_opcode);
    }
    else if constexpr (std::holds_alternative<Row::Steps>(row.action)) {

      // The encoding column says what is fetched, and it is fetched once, before any step, rather than where it is
      // used: the order arguments are evaluated in is unspecified, so a fetch inside a call could land after a memory
      // access the row puts before it.
      std::uint16_t immediate = 0;
      if constexpr (row.immediate_bytes == 2) {
        // Low byte first, the format's rule, which the disassembler follows too.
        const auto low = machine.fetch_immediate();
        immediate = static_cast<std::uint16_t>(machine.fetch_immediate() << 8 | low);
      }
      else if constexpr (row.immediate_bytes == 1)
        immediate = machine.fetch_immediate();
      else
        static_assert(row.immediate_bytes == 0, "the parser allows at most two immediate bytes");
      // What the instruction carries, gathered once its bytes are fetched, for every operand to read.
      const Decoded decoded{.immediate = immediate, .view = view, .opcode = opcode};
      // The displaced address, formed once after both fetches and handed to every operand displaced through it. The
      // machine is told how many bytes were read after the displacement and before the address is formed: the row's
      // immediate bytes, and a latched table's opcode. Whether those reads overlap the forming is the
      // machine's call (on the Z80 they do), and how many its window holds is the machine's to say. Assigned rather
      // than formed by a lambda, which would capture `decoded` and `displacement` by reference and so take their
      // addresses, refusing the tail call below in any build that does not inline it.
      std::uint16_t displaced_address = 0;
      if constexpr (displaced) {
        constexpr std::uint8_t read_after = row.immediate_bytes + (entered_latched ? 1 : 0);
        consteval { check_machine_displaces(read_after, row.line); }
        // Asked again, so that a machine that cannot displace is told so above and not by a failed call besides.
        if constexpr (DisplacingMachine<Machine>)
          displaced_address =
              machine.displaced_address(direct_value_of<*displaced, row.line, std::uint16_t>(machine, decoded),
                  static_cast<std::int8_t>(displacement), read_after);
      }
      // Expanded, not looped: the body is instantiated once per step, and `step` is `constexpr` inside it, which is
      // what lets its contents be template arguments. A `return` here leaves `execute_one`, not the expansion.
      // A reference into `row`, which has static storage, so the expansion statement below can walk it as a range: a
      // range's *address* has to be a constant, and a local copy's would not be.
      constexpr const auto &steps = std::get<Row::Steps>(row.action);
      template for (constexpr auto step: steps) {
        constexpr auto verb = verb_for(step, row.matched, BodyKey, row.line, rules);
        constexpr auto call = call_for(step, row.matched, BodyKey, row.line, rules);
        // After a condition that says no, the rest of the row is skipped, which is where the extra cycles of a taken
        // branch come from. `break` rather than `return`, because abandoning the rest of a row is not abandoning the
        // run: the hand-over below still has to happen, and a `return` here stops the machine at the first untaken
        // branch.
        if (run_step<find_operation(verb, row.line), call>(machine, decoded, displaced_address) == Continue::no)
          break;
      }
    }
    else
      static_assert(false, "a row's action is something new, and nothing here generates it");
    // The row is done, so hand on to the next instruction rather than returning: this hand-over is the run loop.
    [[clang::musttail]] return continue_running(machine, 0, 0, 0);
  }

  // ---------------------------------------------------------------------------
  // One function per instruction
  // ---------------------------------------------------------------------------
  //
  // A row's distinct bodies are the combinations of the slices it *reads*: a catch-all reading none is one function for
  // every opcode it claims, and a row reading two three-bit slices is sixty-four. The functions below enumerate those
  // bodies once each and fill a table's 256 entries by pointing at them.

  // The bits of the opcode that change a row's generated code: those of each slice it reads, as a mask. Three kinds of
  // reference read none: a view is a run-time value, a numeric vocabulary is read straight out of the opcode, and the
  // mnemonic's own references are the disassembler's business; nothing below this line ever looks at `row.pieces`.
  [[nodiscard]] static constexpr std::uint8_t bits_read_by(const Row &row) {
    std::uint8_t read = 0;
    const auto note = [&read, &row](const Reference reference) {
      if (reference.from_view || Compiled::vocabularies()[reference.vocabulary_index].numeric)
        return;
      const auto &slice = row.matched.slices[reference.slice_index];
      read = static_cast<std::uint8_t>(read | slice.place(slice.mask));
    };
    const auto note_operand = [&](const Operand &operand) {
      refract::visit(
          Overloaded{
              [&](const Operand::Vocabulary &vocabulary) { note(vocabulary.reference); },
              [](const OneOf<Operand::Constant, Operand::Named, Operand::Immediate, Operand::Discard> auto &) {},
          },
          operand.kind);
    };
    for (const auto &step: steps_of(row)) {
      if (step.operation_reference)
        note(*step.operation_reference);
      std::ranges::for_each(step.operands, note_operand);
      std::ranges::for_each(step.destinations, note_operand);
    }
    return read;
  }

  // The encoding with every unread variable bit cleared, which names the body this opcode wants: two opcodes of one row
  // share a body exactly when this agrees. The slices `bits_read_by` leaves out must be exactly the ones the generated
  // code does not branch on; a new kind of reference the code branches on has to be noted there, or two opcodes would
  // share a body they disagree about. `disagreements` below checks that they do not.
  [[nodiscard]] static constexpr std::uint8_t body_key(const Row &row, const std::uint8_t opcode) {
    return static_cast<std::uint8_t>(row.matched.opcode_bits | (opcode & bits_read_by(row)));
  }

  // An opcode whose own steps differ from those of the body it shares: which table, which opcode, and the row's line.
  struct Disagreement {
    std::uint8_t table{};
    std::uint8_t opcode{};
    std::size_t line{};
  };

  // Every opcode of every table whose steps, resolved against its own encoding, differ in operation or in call from
  // those resolved against its `body_key`, which are what its body runs. Empty when `bits_read_by` misses nothing this
  // description branches on. Left to a target's tests to call rather than checked in the build, where it adds
  // noticeably to the interpreter's compile time.
  [[nodiscard]] static constexpr std::vector<Disagreement> disagreements() {
    std::vector<Disagreement> result;
    for (const auto table: std::views::iota(0uz, Compiled::tables().size())) {
      const auto &rules = Compiled::tables()[table].rules;
      for (const auto opcode: std::views::iota(0uz, 256uz)) {
        const auto byte = static_cast<std::uint8_t>(opcode);
        const auto &row = Compiled::rows()[Compiled::decoded()[table][opcode].value()];
        const auto key = body_key(row, byte);
        const auto agrees = [&](const Step &step) {
          return verb_for(step, row.matched, byte, row.line, rules) ==
                     verb_for(step, row.matched, key, row.line, rules) &&
                 call_for(step, row.matched, byte, row.line, rules) ==
                     call_for(step, row.matched, key, row.line, rules);
        };
        if (key != byte && !std::ranges::all_of(steps_of(row), agrees))
          result.push_back({.table = static_cast<std::uint8_t>(table), .opcode = byte, .line = row.line});
      }
    }
    return result;
  }

  // One generated function, named by the row it runs, as an index into `Compiled::rows()`, and the encoding that fixes
  // every slice the row reads, which is its `body_key`.
  struct Body {
    std::size_t row{};
    std::uint8_t opcode{};
  };

  // The distinct bodies a table's opcodes run, and for each of its 256 opcodes the index into `bodies` of its own.
  struct Decoding {
    std::vector<Body> bodies;
    std::array<std::uint16_t, 256> body_of{};
  };

  // Enumerates a table's bodies, one per (row, key) its opcodes reach, and which body each opcode uses. Every opcode
  // decodes to some row, since `Compiled` checks that when it is instantiated; `.value()` rather than `*` so that if
  // the check failed and this is instantiated anyway, the second complaint is one readable line.
  [[nodiscard]] static consteval Decoding decoding_for(const std::uint8_t table) {
    Decoding result;
    // Indexed, never searched: `made[row][key]` is the body this row already has for that combination of the slices it
    // reads. Per row as well as per key, because two rows may narrow to the same encoding and are still two rows.
    std::vector<std::array<std::optional<std::uint16_t>, 256>> made(Compiled::rows().size());
    for (const auto opcode: std::views::iota(0uz, 256uz)) {
      const auto row = Compiled::decoded()[table][opcode].value();
      const auto key = body_key(Compiled::rows()[row], static_cast<std::uint8_t>(opcode));
      auto &body = made[row][key];
      if (!body) {
        body = static_cast<std::uint16_t>(result.bodies.size());
        result.bodies.push_back({.row = row, .opcode = key});
      }
      result.body_of[opcode] = *body;
    }
    return result;
  }

  // A table's bodies, promoted out of the `std::vector` `decoding_for` answers in, and which body each of its 256
  // opcodes uses.
  template<std::uint8_t Table>
  static constexpr auto bodies_of = std::define_static_array(decoding_for(Table).bodies);
  template<std::uint8_t Table>
  static constexpr auto body_of = decoding_for(Table).body_of;

  // A table's 256 handlers, one per opcode, pointing at the bodies `bodies_of` enumerated. `execute_one` takes its row
  // and encoding as template arguments, which a `template for` can supply and a loop cannot; filling the entries from
  // the functions made is a loop, because by then they are values.
  template<std::uint8_t Table>
  static constexpr auto dispatch = [] {
    std::array<Handler, bodies_of<Table>.size()> made{};
    template for (constexpr auto at: std::views::iota(0uz, bodies_of<Table>.size())) {
      made[at] = &execute_one<Table, bodies_of<Table>[at].opcode, bodies_of<Table>[at].row>;
    }
    std::array<Handler, 256> handlers{};
    std::ranges::transform(body_of<Table>, handlers.begin(), [&made](const std::uint16_t body) { return made[body]; });
    return handlers;
  }();

  // Starts the next instruction: asks the machine whether there is one, fetches its opcode and tail-calls the entry
  // table's handler for it. Every handler ends here, and this ends in the next handler, so a run of instructions is a
  // chain of tail calls and the stack never grows. The machine decides whether there is a next one: `start_instruction`
  // is where a Z80 takes its interrupt and idles its halt, none of which is the framework's business.
  //
  // Handler-shaped so that the tail call out of a handler is a tail call: the three arguments a fresh instruction has
  // no use for are passed as zero.
  static void continue_running(Machine &machine, std::uint8_t, std::uint8_t, std::uint8_t) {
    if (!machine.start_instruction())
      return;
    const auto opcode = machine.fetch_opcode();
    [[clang::musttail]] return dispatch<Compiled::entry_table>[opcode](machine, 0, 0, opcode);
  }

  // Checks that a displaced row's machine can form its address, and that the bytes the row reads after its
  // displacement are no more than the machine's `displaced_address` accounts for. Either is a diagnostic against the
  // row, rather than a failed call or a machine asked to account for bytes it was not written for.
  static consteval void check_machine_displaces(const std::uint8_t bytes_read, const std::size_t line) {
    if constexpr (!DisplacingMachine<Machine>)
      throw error(line, "this row is displaced, so the machine needs displaced_address and displacement_window_bytes "
                        "(DisplacingMachine in Machine.hpp)");
    else if (bytes_read > Machine::displacement_window_bytes)
      throw error(line, "this row reads " + decimal(bytes_read) +
                            " byte(s) after its displacement and before its address is formed, which is more than "
                            "this machine's displacement_window_bytes allows");
  }

  // Starts the run. The handlers tail-call each other from here on, so this is the only frame the run keeps.
  //
  // The checks that need this class's own scan of the machine run here rather than in `TargetLike`: that its readable
  // locations have distinct names, and that each vocabulary's operations agree about being conditions. This is the one
  // entry point, so they run once regardless. The description on its own was checked when `Compiled` was instantiated.
  static void run(Machine &machine) {
    consteval {
      check_location_names_unique();
      check_conditions_agree();
    }
    continue_running(machine, 0, 0, 0);
  }
};

} // namespace specbolt::refract
