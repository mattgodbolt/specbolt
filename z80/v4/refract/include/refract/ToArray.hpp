#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <ranges>
#include <type_traits>

namespace specbolt::refract {

// Copies what `Make()` returns into a `std::array` of exactly its size.
//
// `Make` is called twice, once for the size and once for the contents, which
// is what `std::regular_invocable` promises is safe: both calls answer the
// same. The array's size must be a constant expression, so the first result would have
// to be a `constexpr` local, and a `std::vector` cannot be one: storage built
// in one constant evaluation is given back when that evaluation ends. This is where
// the vectors the parse works in become the fixed-size arrays that outlive it,
// and the second evaluation is a cost measured in notes/MEASUREMENTS.md.
//
// `Make` is a captureless lambda, so it is a structural type and can be the
// template argument that names the same expression in both places.
template<std::regular_invocable auto Make>
  requires std::ranges::sized_range<std::invoke_result_t<decltype(Make)>>
[[nodiscard]] consteval auto to_array() {
  constexpr auto size = Make().size();
  std::array<std::ranges::range_value_t<decltype(Make())>, size> result{};
  std::ranges::copy(Make(), result.begin());
  return result;
}

} // namespace specbolt::refract
