#pragma once

// Helpers for `std::visit`, which is how refract decides anything by the kind of a variant: every alternative has to be
// handled, so adding one is a compile error at each place that must decide what it means. Testing for one alternative
// with `std::holds_alternative` or `std::get_if` is kept for questions that really are about exactly that alternative.

#include <concepts>

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

} // namespace specbolt::refract
