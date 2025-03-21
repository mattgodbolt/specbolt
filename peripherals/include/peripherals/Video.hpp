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
  static constexpr auto YBorder = 32;
  static constexpr auto ScreenWidth = 256;
  static constexpr auto ScreenHeight = 192;
  static constexpr auto VisibleWidth = XBorder + ScreenWidth + XBorder;
  static constexpr auto VisibleHeight = YBorder + ScreenHeight + YBorder;
  static constexpr auto ColumnCount = ScreenWidth / 8;
  static constexpr auto CyclesPerScanLine = 224;

  explicit Video(const Memory &memory);

  void set_border(const std::uint8_t border) { border_ = border; }
  void set_page(const std::uint8_t page) { page_ = page; }
  bool poll(std::size_t num_cycles);
  bool next_scan_line();

  void blit_to(std::span<std::uint32_t> screen, bool swap_rgb = false) const;

private:
  const Memory &memory_;
  std::uint8_t border_{3};
  std::uint8_t page_{1};
  std::size_t total_cycles_{};
  std::size_t next_line_cycles_{};
  std::size_t current_line_{};
  std::size_t flash_counter_{};
  bool flash_on_{};
  struct ColumnRow {
    std::uint8_t attribute{};
    std::uint8_t pixel{};
  };
  struct Line {
    std::uint8_t border_{};
    std::array<ColumnRow, ColumnCount> columns{};
  };
  std::array<Line, VisibleHeight> lines_{};

  void render_line(std::size_t display_line);
};

} // namespace specbolt
