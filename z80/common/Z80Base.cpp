#ifndef SPECBOLT_MODULES
#include "z80/common/Z80Base.hpp"
#include "peripherals/Memory.hpp"

#include <iostream>
#endif

namespace specbolt {

Flags Z80Base::flags() const { return Flags(regs_.get(RegisterFile::R8::F)); }
void Z80Base::flags(const Flags flags) { regs_.set(RegisterFile::R8::F, flags.to_u8()); }

void Z80Base::halt() {
  halted_ = true;
  regs().pc(regs().pc() - 1);
}

void Z80Base::add_out_handler(OutHandler handler) { out_handlers_.emplace_back(std::move(handler)); }
void Z80Base::add_in_handler(InHandler handler) { in_handlers_.emplace_back(std::move(handler)); }

void Z80Base::out(const std::uint16_t port, const std::uint8_t value) {
  for (const auto &handler: out_handlers_)
    handler(port, value);
}

std::uint8_t Z80Base::in(const std::uint16_t port) {
  std::uint8_t combined_result = 0xff;
  for (const auto &handler: in_handlers_) {
    if (const auto result = handler(port); result.has_value())
      combined_result &= *result;
  }
  return combined_result;
}

void Z80Base::dump() const {
  regs_.dump(std::cout, "");
  std::print(std::cout, "ROM flags : {}, {}, {}, {}\n", memory_.rom_flags()[0], memory_.rom_flags()[1],
      memory_.rom_flags()[2], memory_.rom_flags()[3]);
  std::print(std::cout, "Page      : {}, {}, {}, {}\n", memory_.page_table()[0], memory_.page_table()[1],
      memory_.page_table()[2], memory_.page_table()[3]);
}

} // namespace specbolt
