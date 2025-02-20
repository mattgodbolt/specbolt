#include "peripherals/Keyboard.hpp"

namespace specbolt {

namespace {

std::optional<std::size_t> port_to_row(const std::uint16_t port) {
  switch (port) {
    case 0xfefe: return 0;
    case 0xfdfe: return 1;
    case 0xfbfe: return 2;
    case 0xf7fe: return 3;
    case 0xeffe: return 4;
    case 0xdffe: return 5;
    case 0xbffe: return 6;
    case 0x7ffe: return 7;
    default: return {};
  }
}

std::optional<std::pair<std::size_t, std::size_t>> keycode_to_row_column(const std::int32_t key_code) {
  switch (key_code) {
    case 'j': return std::make_pair(6, 3);
    default: return std::nullopt;
  }
}

} // namespace

std::optional<std::uint8_t> Keyboard::in(const std::uint16_t address) const {
  if (const auto maybe_row = port_to_row(address))
    return static_cast<std::uint8_t>(keys_[*maybe_row].to_ulong());
  return std::nullopt;
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
