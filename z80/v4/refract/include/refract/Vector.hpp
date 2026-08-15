#pragma once

#ifndef SPECBOLT_MODULES
#include <array>
#include <cstddef>
#endif

namespace specbolt::refract {

// A fixed-capacity vector that works during constant evaluation and stays
// structural, so one can be a template argument. `std::inplace_vector` is the
// obvious answer and the wrong one twice over: it is not structural, and gcc
// 16.2's constexpr path supports trivial types only, while these hold
// `std::string_view`.
//
// Everything here is public because structural types have no other option.
// `try_push_back` follows `std::inplace_vector`'s spelling and, like it, leaves
// what a full container means to the caller: overflowing is a description
// asking for more than the format allows, and only the caller knows which
// limit was reached and on which line.
SPECBOLT_EXPORT template<typename T, std::size_t N>
struct Vector {
  std::array<T, N> storage{};
  std::size_t count{};

  static constexpr std::size_t capacity = N;

  [[nodiscard]] constexpr bool try_push_back(const T &value) {
    if (count == N)
      return false;
    storage[count++] = value;
    return true;
  }

  [[nodiscard]] constexpr std::size_t size() const { return count; }
  [[nodiscard]] constexpr bool empty() const { return count == 0; }
  [[nodiscard]] constexpr auto begin() const { return storage.begin(); }
  [[nodiscard]] constexpr auto end() const { return storage.begin() + static_cast<std::ptrdiff_t>(count); }
  [[nodiscard]] constexpr auto begin() { return storage.begin(); }
  [[nodiscard]] constexpr auto end() { return storage.begin() + static_cast<std::ptrdiff_t>(count); }
  [[nodiscard]] constexpr const T &operator[](const std::size_t at) const { return storage[at]; }
  [[nodiscard]] constexpr T &operator[](const std::size_t at) { return storage[at]; }
  // The unused tail counts as well as the used part, which is sound only because
  // nothing here ever shrinks: two vectors holding the same sequence reached it
  // by the same appends, so their spare slots are equally untouched.
  //
  // That is a fact about `storage` and `count`, not about this operator. Two
  // `Call`s are the same template argument when they are *memberwise* equal
  // ([temp.type]), which never consults `operator==`. This is here so that
  // ordinary code can compare one, and it agrees with the language by
  // construction because it is defaulted.
  constexpr bool operator==(const Vector &) const = default;
};

} // namespace specbolt::refract
