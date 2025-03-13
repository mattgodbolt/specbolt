#pragma once

#ifndef SPECBOLT_MODULES
#include "peripherals/Memory.hpp"

#include <array>
#include <cstdint>
#include <span>
#endif

namespace specbolt {

SPECBOLT_EXPORT class Video {
public:
  static constexpr auto XBorder = 32;
  static constexpr auto YBorder = 24;
  static constexpr auto ScreenWidth = 256;
  static constexpr auto ScreenHeight = 192;
  static constexpr auto Width = XBorder + ScreenWidth + XBorder;
  static constexpr auto Height = YBorder + ScreenHeight + YBorder;

  explicit Video(const Memory &memory);

  void set_border(const std::uint8_t border) { border_ = border; }
  bool poll(std::size_t num_cycles);

  [[nodiscard]] std::span<const std::byte> screen() const;

private:
  const Memory &memory_;
  std::array<std::uint32_t, Height * Width> screen_{};
  std::uint8_t border_{3};
  std::size_t total_cycles_{};
  std::size_t next_line_cycles_{};
  std::size_t current_line_{};
  std::size_t flash_counter_{};
  bool flash_on_{};

  void render_line(std::size_t line);
};

} // namespace specbolt
