module;

#include <functional>
#include <iostream>
#include <vector>

export module z80_v2:Mnemonic;

import peripherals;
import z80_common;

namespace specbolt::v2::impl {

struct Mnemonic {
  std::array<char, 15> storage{};
  std::size_t len{};
  constexpr Mnemonic() = default;
  template<std::size_t size>
  // ReSharper disable once CppNonExplicitConvertingConstructor
  constexpr Mnemonic(const char (&name)[size]) { // NOLINT(*-explicit-constructor)
    std::copy(name, name + size - 1, storage.begin());
    len = size - 1;
  }
  template<typename String>
  // ReSharper disable once CppNonExplicitConvertingConstructor
  constexpr Mnemonic(const String &name) { // NOLINT(*-explicit-constructor)
    std::ranges::copy(name, storage.begin());
    len = name.size();
  }
  [[nodiscard]] constexpr std::string_view view() const { return {storage.data(), len}; }
  [[nodiscard]] constexpr std::string str() const { return {storage.data(), len}; }
};

} // namespace specbolt::v2::impl
