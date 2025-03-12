#include "z80/common/Z80Base.hpp"

#include "peripherals/Memory.hpp"

#include <iostream>

namespace specbolt {

Flags Z80Base::flags() const { return Flags(regs_.get(RegisterFile::R8::F)); }
void Z80Base::flags(const Flags flags) { regs_.set(RegisterFile::R8::F, flags.to_u8()); }

void Z80Base::halt() {
  halted_ = true;
  registers().pc(registers().pc() - 1);
}

void Z80Base::add_out_handler(OutHandler handler) { out_handlers_.emplace_back(std::move(handler)); }
void Z80Base::add_in_handler(InHandler handler) { in_handlers_.emplace_back(std::move(handler)); }

void Z80Base::out(const std::uint16_t port, const std::uint8_t value) {
  for (const auto &handler: out_handlers_)
    handler(port, value);
}

std::uint8_t Z80Base::in(const std::uint16_t port) {
  uint8_t combined_result = 0xff;
  for (const auto &handler: in_handlers_) {
    if (const auto result = handler(port); result.has_value())
      combined_result &= *result;
  }
  return combined_result;
}

void Z80Base::dump() const { regs_.dump(std::cout, ""); }

} // namespace specbolt
