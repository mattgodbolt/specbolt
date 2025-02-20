#pragma once

#include "peripherals/Memory.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace specbolt {

class Video {
public:
  static constexpr auto Width = 640 - 64;
  static constexpr auto Height = 480 - 48;

  explicit Video(const Memory &memory);

  void set_border(std::uint8_t border) { border_ = border; }
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
