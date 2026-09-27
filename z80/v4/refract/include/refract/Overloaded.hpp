#pragma once

// Helpers for `std::visit`, which is how refract decides anything by the kind of a variant: every alternative has to be
// handled, so adding one is a compile error at each place that must decide what it means. Testing for one alternative
// with `std::holds_alternative` or `std::get_if` is kept for questions that really are about exactly that alternative.

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <variant>

namespace specbolt::refract {

// A visitor made of lambdas, one per alternative or group of alternatives.
template<typename... Lambdas>
struct Overloaded : Lambdas... {
  using Lambdas::operator()...;
};

// Whether `T` is one of `Alternatives`: for a lambda that handles several alternatives the same way and names them,
// `[](const OneOf<A, B> auto &) {...}`, so that a new alternative matches no lambda rather than falling into a
// catch-all.
template<typename T, typename... Alternatives>
concept OneOf = (std::same_as<T, Alternatives> || ...);

// Calls whichever of `visitor`'s overloads takes the alternative `variant` holds, and returns what it returns: what
// `std::visit` does, for one variant. It asks `index()` in turn rather than building `std::visit`'s dispatch table,
// which is far cheaper during constant evaluation, where refract does nearly all of its visiting. Every alternative's
// overload is still instantiated, so a missing one is still a compile error.
template<std::size_t At = 0, typename Visitor, typename Variant>
constexpr decltype(auto) visit(Visitor &&visitor, Variant &&variant) {
  if constexpr (At + 1 == std::variant_size_v<std::remove_cvref_t<Variant>>)
    return visitor(*std::get_if<At>(&variant));
  else if (variant.index() == At)
    return visitor(*std::get_if<At>(&variant));
  else
    return visit<At + 1>(visitor, variant);
}

} // namespace specbolt::refract
