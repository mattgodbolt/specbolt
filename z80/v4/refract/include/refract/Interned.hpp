#pragma once

#include <algorithm>
#include <cstddef>
#include <meta>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

namespace specbolt::refract {

// A view of a constant that a template argument can hold: text, or a list of values. `std::string_view` and
// `std::span` would say the same, but their members are private, so neither is structural and neither can be part of
// a template argument. This is the same pointer and count with both public.
//
// The standard library supplies the storage. During constant evaluation the contents are copied once into static
// storage by `std::define_static_string` or `std::define_static_array`, which hand back the same object for the same
// contents wherever they are asked. That is what makes two handlers built from equal text the same template argument:
// the pointers compare equal because the strings do. A view of text built at run time, as a test of the parser
// builds one, points at the text it was given, which the description outlives.
//
// Empty is always a null pointer, whichever way it was made, so that an empty view made at compile time and a default
// one are the same template argument.
template<typename T>
struct Interned {
  const T *pointer{};
  std::size_t count{};

  constexpr Interned() = default;

  // Text, copied once during constant evaluation and viewed where it lies at run time. Implicit, as
  // `std::string_view`'s own conversions are, so that `Spelling{"i"}` reads as it would with a string.
  template<std::size_t N>
    requires std::same_as<T, char>
  constexpr Interned(const char (&text)[N]) :
      Interned(std::string_view{text, N - 1}) {} // NOLINT(*-explicit-constructor)
  constexpr Interned(const std::string_view text) // NOLINT(*-explicit-constructor)
    requires std::same_as<T, char>
      : count(text.size()) {
    if (text.empty())
      return;
    if consteval {
      pointer = std::define_static_string(text);
    }
    else {
      pointer = text.data();
    }
  }

  // A list, copied once. Only during constant evaluation: at run time there is nowhere for the copy to live.
  template<std::ranges::input_range Range>
    requires(!std::same_as<T, char>)
  consteval explicit Interned(Range &&values) {
    const auto stored = std::define_static_array(std::forward<Range>(values));
    if (!stored.empty()) {
      pointer = stored.data();
      count = stored.size();
    }
  }

  [[nodiscard]] constexpr std::size_t size() const { return count; }
  [[nodiscard]] constexpr bool empty() const { return count == 0; }
  [[nodiscard]] constexpr const T *begin() const { return pointer; }
  [[nodiscard]] constexpr const T *end() const { return pointer + count; }
  [[nodiscard]] constexpr const T &operator[](const std::size_t at) const { return pointer[at]; }
  [[nodiscard]] constexpr std::string_view view() const
    requires std::same_as<T, char>
  {
    return {pointer, count};
  }

  // By contents, which is what a pointer comparison would also say for two views made at compile time, and what it
  // would not say for a view made at run time.
  constexpr bool operator==(const Interned &other) const { return std::ranges::equal(*this, other); }
};

} // namespace specbolt::refract
