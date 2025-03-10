#pragma once

#include "Memory.hpp"
#include "support.hpp"

void write_sequence(
    std::uint16_t base_address, specbolt::Memory &memory, std::convertible_to<std::uint8_t> auto... bytes) {
  [&]<std::size_t... Idx>(std::index_sequence<Idx...>) {
    (memory.write(static_cast<std::uint16_t>(base_address + Idx), static_cast<std::uint8_t>(bytes)), ...);
  }(std::make_index_sequence<sizeof...(bytes)>());
}
