#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>

namespace specbolt::refract {

// A fixed-capacity vector that works during constant evaluation and is structural, so it, and anything holding one, can
// be a template argument. `std::inplace_vector` is not structural; notes/FINDINGS.md has the rest of why it is not used
// here.
//
// Everything here is public because a structural type's members must be. `try_push_back` reports a full container
// rather than throwing, like `std::inplace_vector`'s, and leaves what that means to the caller: only it knows which of
// the format's limits was reached, and on which line.
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
  // Indexing past the count is a mistake in this library rather than in a description, so it throws rather than reading
  // a default-constructed slot.
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
  // Defaulted, so it compares the unused tail as well as the used part. That is sound because nothing here ever
  // shrinks: two vectors holding the same sequence reached it by the same appends, and their spare slots are equally
  // untouched. Template-argument equivalence compares members the same way and never consults this operator; it is here
  // for ordinary code, and agrees.
  constexpr bool operator==(const Vector &) const = default;
};

} // namespace specbolt::refract
