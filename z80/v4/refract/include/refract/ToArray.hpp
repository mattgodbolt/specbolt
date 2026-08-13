#pragma once

#include <algorithm>
#include <array>
#include <ranges>

namespace specbolt::refract {

// The one place compile-time data becomes run-time data.
//
// Everything that reads a description works in `std::vector`, because that is
// how one writes a parser. None of those vectors can survive: storage allocated
// during constant evaluation must be given back before that evaluation ends, so
// no `constexpr` variable can hold one. Whatever outlives the parse has to be a
// fixed-size array, and an array's size has to be known before it is filled.
//
// Hence twice: once to ask how big the answer is, and once for the answer.
// `Make` is a captureless lambda -- a structural type, and therefore a legal
// template argument -- which is what lets the same expression be evaluated in
// both places. It costs a second parse and buys a pipeline in which nothing but
// this function has to know a count in advance.
template<auto Make>
[[nodiscard]] consteval auto to_array() {
  constexpr auto size = Make().size();
  std::array<std::ranges::range_value_t<decltype(Make())>, size> result{};
  std::ranges::copy(Make(), result.begin());
  return result;
}

} // namespace specbolt::refract
