#pragma once

#include "peripherals/Memory.hpp"
#include "z80/common/Z80Base.hpp"
#include "z80/v4/Instructions.hpp"
#include "z80/v4/Shapes.hpp"

#include <cstdint>
#include <meta>

namespace specbolt::v4 {

class Z80 : public Z80Base {
public:
  explicit Z80(Scheduler &scheduler, Memory &memory) : Z80Base(scheduler, memory) {}

  void execute_one();

  std::uint8_t read_immediate();
  std::uint16_t read_immediate16();

  std::uint8_t read(std::uint16_t address);
  void write(std::uint16_t address, std::uint8_t byte);

  std::uint8_t pop8();
  std::uint16_t pop16();
  void push8(std::uint8_t value);
  void push16(std::uint16_t value);

  // ----------------------------------------------------------------------
  // Three base dispatchers + three CB dispatchers, all from the same
  // shape generators with different RegSets.
  consteval {
    inject_dispatcher("dispatch_base", hl_regs, BaseOpcodes::Include);
    inject_dispatcher("dispatch_dd",   ix_regs, BaseOpcodes::Skip);
    inject_dispatcher("dispatch_fd",   iy_regs, BaseOpcodes::Skip);
    inject_cb_dispatcher("dispatch_cb", hl_regs);
  }
};

} // namespace specbolt::v4
