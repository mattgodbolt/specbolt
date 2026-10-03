#pragma once

#include "refract/TableError.hpp"

#include <array>
#include <cstddef>
#include <meta>
#include <stdexcept>
#include <string>

namespace specbolt::refract {

// A fixed-capacity vector that works during constant evaluation, for the parsed description, which outlives the
// evaluation that built it and so cannot hold a `std::vector`. It is what `std::inplace_vector` is for, and stands in
// for it until both standard libraries this builds with can use theirs here; notes/FINDINGS.md has why neither can yet.
//
// `push_back` throws when the container is full, naming the capacity and the element type; during constant evaluation
// that throw is the compile error. Which line of a description was being read is not this container's business:
// `at_line` puts it in front.
template<typename T, std::size_t N>
class Vector {
public:
  static constexpr std::size_t capacity = N;
  // The element's name for the diagnostic, reflected once here rather than in `push_back`: a reflection call in a
  // function body would make the function consteval, and the parser also runs at run time under test.
  static constexpr std::string_view element_name =
      std::meta::has_identifier(^^T) ? std::meta::identifier_of(^^T) : std::meta::display_string_of(^^T);

  constexpr void push_back(const T &value) {
    if (count_ == N)
      throw std::length_error("more than " + decimal(N) + " " + std::string(element_name));
    storage_[count_++] = value;
  }

  [[nodiscard]] constexpr std::size_t size() const { return count_; }
  [[nodiscard]] constexpr bool empty() const { return count_ == 0; }
  [[nodiscard]] constexpr auto begin() const { return storage_.begin(); }
  [[nodiscard]] constexpr auto end() const { return storage_.begin() + static_cast<std::ptrdiff_t>(count_); }
  [[nodiscard]] constexpr auto begin() { return storage_.begin(); }
  [[nodiscard]] constexpr auto end() { return storage_.begin() + static_cast<std::ptrdiff_t>(count_); }
  // Indexing past the count is a mistake in this library rather than in a description, so it throws rather than reading
  // a default-constructed slot.
  [[nodiscard]] constexpr const T &operator[](const std::size_t at) const {
    if (at >= count_)
      throw std::out_of_range("index past the end of a Vector");
    return storage_[at];
  }
  [[nodiscard]] constexpr T &operator[](const std::size_t at) {
    if (at >= count_)
      throw std::out_of_range("index past the end of a Vector");
    return storage_[at];
  }
  [[nodiscard]] constexpr const T *data() const { return storage_.data(); }
  // Compares the unused tail as well as the used part. That is sound because nothing here ever shrinks: two vectors
  // holding the same sequence reached it by the same appends, and their spare slots are equally untouched. Written out
  // rather than defaulted so that it is instantiated only where two vectors are compared, and an element type needs an
  // `==` only if they are (FINDINGS.md).
  constexpr bool operator==(const Vector &other) const { return count_ == other.count_ && storage_ == other.storage_; }

private:
  std::array<T, N> storage_{};
  std::size_t count_{};
};

} // namespace specbolt::refract
