#include "Keyboard.hpp"

namespace specbolt {

namespace {

std::optional<std::pair<std::size_t, std::size_t>> keycode_to_row_column(const std::int32_t key_code) {
  // These are SDL keycodes but we avoid an SDL dep here.
  switch (key_code) {
    // Row 0
    case 0x400000e1u: // left shift
    case 0x400000e5u: // right shift
      return std::make_pair(0, 0);
    case 'z': return std::make_pair(0, 1);
    case 'x': return std::make_pair(0, 2);
    case 'c': return std::make_pair(0, 3);
    case 'v': return std::make_pair(0, 4);
    // Row 1
    case 'a': return std::make_pair(1, 0);
    case 's': return std::make_pair(1, 1);
    case 'd': return std::make_pair(1, 2);
    case 'f': return std::make_pair(1, 3);
    case 'g': return std::make_pair(1, 4);
    // Row 2
    case 'q': return std::make_pair(2, 0);
    case 'w': return std::make_pair(2, 1);
    case 'e': return std::make_pair(2, 2);
    case 'r': return std::make_pair(2, 3);
    case 't': return std::make_pair(2, 4);
    // Row 3
    case '1': return std::make_pair(3, 0);
    case '2': return std::make_pair(3, 1);
    case '3': return std::make_pair(3, 2);
    case '4': return std::make_pair(3, 3);
    case '5': return std::make_pair(3, 4);
    // Row 4
    case '0': return std::make_pair(4, 0);
    case '9': return std::make_pair(4, 1);
    case '8': return std::make_pair(4, 2);
    case '7': return std::make_pair(4, 3);
    case '6': return std::make_pair(4, 4);
    // Row 5
    case 'p': return std::make_pair(5, 0);
    case 'o': return std::make_pair(5, 1);
    case 'i': return std::make_pair(5, 2);
    case 'u': return std::make_pair(5, 3);
    case 'y': return std::make_pair(5, 4);
    // Row 6
    case '\r': return std::make_pair(6, 0);
    case 'l': return std::make_pair(6, 1);
    case 'k': return std::make_pair(6, 2);
    case 'j': return std::make_pair(6, 3);
    case 'h': return std::make_pair(6, 4);
    // Row 7
    case ' ': return std::make_pair(7, 0);
    case 0x400000e0u: // left ctrl
    case 0x400000e4u: // right ctrl
      return std::make_pair(7, 1);
    case 'm': return std::make_pair(7, 2);
    case 'n': return std::make_pair(7, 3);
    case 'b': return std::make_pair(7, 4);

    default: return std::nullopt;
  }
}

} // namespace

std::optional<std::uint8_t> Keyboard::in(const std::uint16_t address) const {
  // Keyboard responds to any even address.
  if (address & 1)
    return std::nullopt;

  std::uint8_t keys = 0xff;
  const std::uint8_t res = address >> 8;
  // The low bit in the top address picks the row. In the presence of multiple low bits, the results "and" together.
  for (size_t row = 0; row < 8; ++row)
    if ((res & (1 << row)) == 0)
      keys &= static_cast<std::uint8_t>(keys_[row].to_ulong() ^ 0xff);

  return keys;
}

void Keyboard::key_down(const std::int32_t key_code) {
  if (const auto maybe_row_column = keycode_to_row_column(key_code))
    keys_[maybe_row_column->first][maybe_row_column->second] = true;
}

void Keyboard::key_up(const std::int32_t key_code) {
  if (const auto maybe_row_column = keycode_to_row_column(key_code))
    keys_[maybe_row_column->first][maybe_row_column->second] = false;
  ;
}


} // namespace specbolt
