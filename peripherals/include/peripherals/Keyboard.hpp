#pragma once

#ifndef SPECBOLT_IN_MODULE
#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#endif

namespace specbolt {

SPECBOLT_EXPORT class Keyboard {
public:
  void key_down(std::int32_t key_code);
  void key_up(std::int32_t key_code);

  [[nodiscard]] std::optional<std::uint8_t> in(std::uint16_t address) const;

private:
  static constexpr auto Columns = 5;
  static constexpr auto Rows = 8;
  std::array<std::bitset<Columns>, Rows> keys_{};
};

} // namespace specbolt
