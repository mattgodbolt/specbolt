#ifndef SPECBOLT_MODULES
#include "peripherals/Video.hpp"

#include <algorithm>
#include <format>
#include <span>
#endif

namespace specbolt {

namespace {

constexpr std::array palette{
    0xff000000,
    0xff0000cd,
    0xffcd0000,
    0xffcd00cd,
    0xff00cd00,
    0xff00cdcd,
    0xffcdcd00,
    0xffcdcdcd,
    0xff000000,
    0xff0000ff,
    0xffff0000,
    0xffff00ff,
    0xff00ff00,
    0xff00ffff,
    0xffffff00,
    0xffffffff,
};
constexpr std::array palette_swapped = [] {
  std::array<std::uint32_t, 16> result{};
  for (auto i = 0zu; i < 16zu; ++i)
    result[i] = (palette[i] & 0xff00ff00u) | (palette[i] & 0xff0000) >> 16 | (palette[i] & 0xff) << 16;
  return result;
}();

constexpr auto PalTotalLines = 312zu;
constexpr auto VSyncLines = PalTotalLines - Video::VisibleHeight;
// constexpr auto HSyncPixels = 64;
constexpr auto FramesPerFlash = 16;

constexpr auto AttributeDataOffset = 0x1800;

} // namespace

Video::Video(const Memory &memory) : memory_(memory) {}

bool Video::poll(const std::size_t num_cycles) {
  bool irq = false;
  total_cycles_ += num_cycles;
  while (total_cycles_ > next_line_cycles_) {
    if (next_scan_line())
      irq = true;
    next_line_cycles_ += CyclesPerScanLine;
  }
  return irq;
}

bool Video::next_scan_line() {
  render_line(current_line_);
  current_line_ = (current_line_ + 1) % PalTotalLines;
  if (current_line_ == 0) {
    if (++flash_counter_ == FramesPerFlash) {
      flash_counter_ = 0;
      flash_on_ = !flash_on_;
    }
    return true;
  }
  return false;
}

void Video::blit_to(const std::span<std::uint32_t> screen, bool swap_rgb) const {
  if (screen.size() != VisibleWidth * VisibleHeight)
    throw std::runtime_error(std::format("Bad screen size ({} vs {})", screen.size(), VisibleWidth * VisibleHeight));
  const auto &pal = swap_rgb ? palette_swapped : palette;
  for (auto y = 0uz; y < VisibleHeight; ++y) {
    const auto &[border, columns] = lines_[y];
    auto line_span = screen.subspan(y * VisibleWidth, VisibleWidth);
    if (y < YBorder || y >= YBorder + ScreenHeight) {
      std::ranges::fill(line_span, pal[border]);
      continue;
    }
    // Left and right borders.
    std::ranges::fill(line_span.subspan(0, XBorder), pal[border]);
    std::ranges::fill(line_span.subspan(XBorder + ScreenWidth, XBorder), pal[border]);

    const auto display_span = line_span.subspan(XBorder, ScreenWidth);
    for (std::size_t x = 0; x < ColumnCount; ++x) {
      const auto pixel_data = columns[x].pixel;
      const auto attributes = columns[x].attribute;
      const auto invert = attributes & 0x80 && flash_on_;
      const auto brightness = attributes & 0x40 ? 0x08u : 0x00u;
      const auto index_1 = (attributes >> 3u & 0x07u) + brightness;
      const auto index_2 = (attributes & 0x07u) + brightness;
      const auto paper_color = invert ? pal[index_1] : pal[index_2];
      const auto pen_color = invert ? pal[index_2] : pal[index_1];
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
