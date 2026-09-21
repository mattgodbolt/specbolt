#pragma once

#include "refract/TableError.hpp"

#include <array>
#include <cstddef>
#include <meta>
#include <stdexcept>
#include <string>

namespace specbolt::refract {

// A fixed-capacity vector that works during constant evaluation and is structural, so it, and anything holding one, can
// be a template argument. Nothing requires `std::inplace_vector` to be structural, and the implementations tried are
// not; notes/FINDINGS.md has the rest of why it is not used here.
//
// Everything here is public because a structural type's members must be. `push_back` throws when the container is
// full, naming the capacity and the element type; during constant evaluation that throw is the compile error. Which
// line of a description was being read is not this container's business: `at_line` puts it in front.
template<typename T, std::size_t N>
struct Vector {
  std::array<T, N> storage{};
  std::size_t count{};

  static constexpr std::size_t capacity = N;

  constexpr void push_back(const T &value) {
    if (count == N)
      throw std::length_error("more than " + decimal(N) + " " + std::string(std::meta::display_string_of(^^T)));
    storage[count++] = value;
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
