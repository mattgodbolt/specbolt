#ifndef SPECBOLT_MODULES
#include "z80/v4/Disassembler.hpp"

#include "Table.hpp"
#include "peripherals/Memory.hpp"
#include "refract/Disassemble.hpp"

#include <utility>
#endif

namespace specbolt::v4 {

// The whole of the Z80's part in disassembling itself: say where the bytes come
// from. Following prefixes, applying a view's renaming and rendering the
// lowered pieces are all facts about the description rather than about this
// chip, so `refract` does them for any description.
Disassembled disassemble(const Memory &memory, const std::uint16_t address) {
  auto [text, length] = refract::disassemble(description, address,
      [&](const std::size_t offset) { return memory.read(static_cast<std::uint16_t>(address + offset)); });
  return {std::move(text), length};
}

} // namespace specbolt::v4
