#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>

namespace specbolt::refract {

// A fixed-capacity vector that works during constant evaluation and is
// structural, so it, and anything holding one, can be a template argument.
// `std::inplace_vector` is neither: it is not structural, and the standard
// library's constant evaluation of it stops short of an element type holding a
// `std::string_view` (notes/FINDINGS.md).
//
// Everything here is public because a structural type's members must be.
// `try_push_back` reports a full container rather than throwing, like
// `std::inplace_vector`'s, and leaves what that means to the caller: only it
// knows which of the format's limits was reached, and on which line.
template<typename T, std::size_t N>
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
  // Indexing past the count is a mistake in this library rather than in a
  // description, so it throws rather than reading a default-constructed slot.
  [[nodiscard]] constexpr const T &operator[](const std::size_t at) const {
    if (at >= count)
      throw std::out_of_range("index past the end of a Vector");
    return storage[at];
  }
  [[nodiscard]] constexpr T &operator[](const std::size_t at) {
    if (at >= count)
      throw std::out_of_range("index past the end of a Vector");
    return storage[at];
  }
  [[nodiscard]] constexpr const T *data() const { return storage.data(); }
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
