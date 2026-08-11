#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/v4/TableError.hpp"

#include <array>
#include <cstddef>
#endif

namespace specbolt::v4 {

// A fixed-capacity vector that works during constant evaluation and stays
// structural, so one can be a template argument. `std::inplace_vector` is the
// obvious answer and the wrong one for now: gcc 16.2's constexpr path supports
// trivial types only, and these hold `std::string_view`.
//
// Everything here is public because structural types have no other option.
SPECBOLT_EXPORT template<typename T, std::size_t N>
struct Vector {
  std::array<T, N> storage{};
  std::size_t count{};

  static constexpr std::size_t capacity = N;

  // Overflowing is a table that asks for more than the CPU description allows,
  // so the caller says what was too long and where.
  constexpr void push_back(const T &value, const std::size_t line, const std::string_view what) {
    if (count == N)
      throw table_error(line, what);
    storage[count++] = value;
  }

  [[nodiscard]] constexpr std::size_t size() const { return count; }
  [[nodiscard]] constexpr bool empty() const { return count == 0; }
  [[nodiscard]] constexpr auto begin() const { return storage.begin(); }
  [[nodiscard]] constexpr auto end() const { return storage.begin() + static_cast<std::ptrdiff_t>(count); }
  [[nodiscard]] constexpr auto begin() { return storage.begin(); }
  [[nodiscard]] constexpr auto end() { return storage.begin() + static_cast<std::ptrdiff_t>(count); }
  [[nodiscard]] constexpr const T &operator[](const std::size_t at) const { return storage[at]; }
  [[nodiscard]] constexpr T &operator[](const std::size_t at) { return storage[at]; }
  [[nodiscard]] constexpr const T &back() const { return storage[count - 1]; }
  constexpr bool operator==(const Vector &) const = default;
};

} // namespace specbolt::v4
