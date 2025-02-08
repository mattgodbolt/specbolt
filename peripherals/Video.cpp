#include "peripherals/Video.hpp"
#include <ranges>
#include <algorithm>

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
constexpr auto XBorder = 32;
constexpr auto YBorder = 24;
constexpr auto ScreenWidth = 256;
constexpr auto ScreenHeight = 192;
constexpr auto ScaleFactor = 2;
constexpr auto CyclesPerScanLine = 224;
constexpr auto FramesPerFlash = 16;

constexpr auto PixelDataAddress = 0x4000;
constexpr auto AttributeDataAddress = 0x5800;
constexpr auto ColumnCount = 32;

static_assert(YBorder + ScreenHeight * ScaleFactor + YBorder == Video::Height);
static_assert(XBorder + ScreenWidth * ScaleFactor + XBorder == Video::Width);

} // namespace

Video::Video(const Memory &memory) : memory_(memory) {}

void Video::poll(const std::size_t num_cycles) {
  total_cycles_ += num_cycles;
  while (total_cycles_ > next_line_cycles_) {
    render_line(current_line_);
    current_line_ = (current_line_ + 1) % (Height + VSyncLines);
    if (current_line_ == 0) {
      if (++flash_counter_ == FramesPerFlash) {
        flash_counter_ = 0;
        flash_on_ = !flash_on_;
      }
    }
    next_line_cycles_ += CyclesPerScanLine;
  }
  // TODO irqs
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
  if (line >= ScreenHeight * ScaleFactor) {
    // Bottom border.
    std::ranges::fill(line_span, palette[border_]);
    return;
  }

  // Left and right borders.
  std::ranges::fill(line_span.subspan(0, XBorder), palette[border_]);
  std::ranges::fill(line_span.subspan(XBorder + ScreenWidth * ScaleFactor, XBorder), palette[border_]);

  const auto display_span = line_span.subspan(XBorder, ScreenWidth * ScaleFactor);
  const auto screen_line = line / ScaleFactor;
  const auto y76 = screen_line >> 8;
  const auto y543 = screen_line >> 3 & 0x07;
  const auto y210 = screen_line & 0x07;
  const auto screen_address = PixelDataAddress + (y76 << 11) + (y543 << 5) + y210;
  const auto char_row = screen_line / 8;
  for (std::size_t x = 0; x < ColumnCount; ++x) {
    const auto pixel_data = memory_.read(static_cast<std::uint16_t>(screen_address + x));
    const auto attributes = memory_.read(static_cast<std::uint16_t>(AttributeDataAddress + char_row * ColumnCount + x));
    const auto pen_color = palette[attributes & 0x07];
    const auto paper_color = palette[attributes >> 3 & 0x07];
    const auto invert = attributes & 0x80 && flash_on_;
    for (std::size_t bit = 0; bit < 8; ++bit) {
      const auto colour = ((pixel_data & (1 << bit)) ^ invert) ? pen_color : paper_color;
      std::ranges::fill(display_span.subspan((x * 8u + bit) * ScaleFactor, ScaleFactor), colour);
    }
  }
}

std::span<const std::byte> Video::screen() const {
  return {reinterpret_cast<const std::byte *>(screen_.data()), screen_.size() * sizeof(std::uint32_t)};
}

} // namespace specbolt
