#ifndef SPECBOLT_MODULES
#include "peripherals/Video.hpp"

#include <algorithm>
#include <span>
#endif

namespace specbolt {

namespace {

constexpr std::array<std::uint32_t, 16> palette{
    0x000000,
    0x0000cd,
    0xcd0000,
    0xcd00cd,
    0x00cd00,
    0x00cdcd,
    0xcdcd00,
    0xcdcdcd,
    0x000000,
    0x0000ff,
    0xff0000,
    0xff00ff,
    0x00ff00,
    0x00ffff,
    0xffff00,
    0xffffff,
};

constexpr auto VSyncLines = 48;
// constexpr auto HSyncPixels = 64;
constexpr auto CyclesPerScanLine = 224;
constexpr auto FramesPerFlash = 16;

constexpr auto AttributeDataOffset = 0x1800;
constexpr auto ColumnCount = 32;


} // namespace

Video::Video(const Memory &memory) : memory_(memory) {}

bool Video::poll(const std::size_t num_cycles) {
  bool irq = false;
  total_cycles_ += num_cycles;
  while (total_cycles_ > next_line_cycles_) {
    render_line(current_line_);
    current_line_ = (current_line_ + 1) % (Height + VSyncLines);
    if (current_line_ == 0) {
      irq = true;
      if (++flash_counter_ == FramesPerFlash) {
        flash_counter_ = 0;
        flash_on_ = !flash_on_;
      }
    }
    next_line_cycles_ += CyclesPerScanLine;
  }
  return irq;
}

void Video::render_line(std::size_t line) {
  if (line < VSyncLines) {
    return;
  }
  line -= VSyncLines;
  std::span line_span(screen_.data() + line * Width, Width);
  if (line < YBorder) {
    // Top border.
    std::ranges::fill(line_span, palette[border_]);
    return;
  }
  line -= YBorder;
  if (line >= ScreenHeight) {
    // Bottom border.
    std::ranges::fill(line_span, palette[border_]);
    return;
  }

  // Left and right borders.
  std::ranges::fill(line_span.subspan(0, XBorder), palette[border_]);
  std::ranges::fill(line_span.subspan(XBorder + ScreenWidth, XBorder), palette[border_]);

  const auto display_span = line_span.subspan(XBorder, ScreenWidth);
  const auto y76 = (line >> 6) & 0x03;
  const auto y543 = (line >> 3) & 0x07;
  const auto y210 = line & 0x07;
  const auto screen_offset = (y76 << 11) + (y543 << 5) + (y210 << 8);
  const auto char_row = line / 8;
  for (std::size_t x = 0; x < ColumnCount; ++x) {
    const auto pixel_data = memory_.raw_read(page_, static_cast<std::uint16_t>(screen_offset + x));
    const auto attributes =
        memory_.raw_read(page_, static_cast<std::uint16_t>(AttributeDataOffset + char_row * ColumnCount + x));
    const auto pen_color = palette[attributes & 0x07];
    const auto paper_color = palette[attributes >> 3 & 0x07];
    const auto invert = attributes & 0x80 && flash_on_;
    for (std::size_t bit = 0; bit < 8; ++bit) {
      const bool pixel_on = pixel_data & (1 << (7 - bit));
      const auto colour = pixel_on == invert ? paper_color : pen_color;
      display_span[x * 8u + bit] = colour;
    }
  }
}

std::span<const std::byte> Video::screen() const {
  return {reinterpret_cast<const std::byte *>(screen_.data()), screen_.size() * sizeof(std::uint32_t)};
}

} // namespace specbolt
