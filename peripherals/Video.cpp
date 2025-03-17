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

constexpr auto PalTotalLines = 312zu;
constexpr auto VSyncLines = PalTotalLines - Video::VisibleHeight;
// constexpr auto HSyncPixels = 64;
constexpr auto CyclesPerScanLine = 224;
constexpr auto FramesPerFlash = 16;

constexpr auto AttributeDataOffset = 0x1800;

} // namespace

Video::Video(const Memory &memory) : memory_(memory) {}

bool Video::poll(const std::size_t num_cycles) {
  bool irq = false;
  total_cycles_ += num_cycles;
  while (total_cycles_ > next_line_cycles_) {
    render_line(current_line_);
    current_line_ = (current_line_ + 1) % PalTotalLines;
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

void Video::blit_to(const std::span<std::uint32_t> screen) const {
  if (screen.size() != VisibleWidth * VisibleHeight)
    throw std::runtime_error("Bad screen size");
  for (auto y = 0uz; y < VisibleHeight; ++y) {
    const auto &[border, columns] = lines_[y];
    auto line_span = screen.subspan(y * VisibleWidth, VisibleWidth);
    if (y < YBorder || y >= YBorder + ScreenHeight) {
      std::ranges::fill(line_span, palette[border]);
      continue;
    }
    // Left and right borders.
    std::ranges::fill(line_span.subspan(0, XBorder), palette[border]);
    std::ranges::fill(line_span.subspan(XBorder + ScreenWidth, XBorder), palette[border]);

    const auto display_span = line_span.subspan(XBorder, ScreenWidth);
    for (std::size_t x = 0; x < ColumnCount; ++x) {
      const auto pixel_data = columns[x].pixel;
      const auto attributes = columns[x].attribute;
      const auto invert = attributes & 0x80 && flash_on_;
      const auto index_1 = attributes >> 3u & 0x07u;
      const auto index_2 = attributes & 0x07u;
      const auto paper_color = invert ? palette[index_1] : palette[index_2];
      const auto pen_color = invert ? palette[index_2] : palette[index_1];
      for (std::size_t bit = 0; bit < 8; ++bit) {
        display_span[x * 8u + bit] = pixel_data & 1 << (7 - bit) ? paper_color : pen_color;
      }
    }
  }
}

void Video::render_line(const std::size_t display_line) {
  if (display_line < VSyncLines)
    return;
  auto &[border, columns] = lines_[display_line - VSyncLines];
  border = border_;
  const auto y = display_line - YBorder - VSyncLines;
  if (y >= ScreenHeight) // also handles y < 0 as unsigned above does that...
    return;

  const auto y76 = (y >> 6) & 0x03;
  const auto y543 = (y >> 3) & 0x07;
  const auto y210 = y & 0x07;
  const auto screen_offset = (y76 << 11) + (y543 << 5) + (y210 << 8);
  const auto char_row = y / 8;
  for (std::size_t x = 0; x < ColumnCount; ++x) {
    columns[x].pixel = memory_.raw_read(page_, static_cast<std::uint16_t>(screen_offset + x));
    columns[x].attribute =
        memory_.raw_read(page_, static_cast<std::uint16_t>(AttributeDataOffset + char_row * ColumnCount + x));
  }
}

} // namespace specbolt
