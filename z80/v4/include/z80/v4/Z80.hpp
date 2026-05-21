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
  // All the dispatchers, generated from one shape library:
  //   base/dd/fd   share the same shape set, three RegSets
  //   cb           operand at regs_.wz()
  //   ddcb/fdcb    operand always at regs_.wz(), with the undocumented
  //                "side store" to r[z] for non-bit ops (same shape for both)
  //   ed           one-offs + block ops
  consteval {
    inject_dispatcher("dispatch_base", hl_regs, BaseOpcodes::Include);
    inject_dispatcher("dispatch_dd",   ix_regs, BaseOpcodes::Skip);
    inject_dispatcher("dispatch_fd",   iy_regs, BaseOpcodes::Skip);
    inject_cb_dispatcher("dispatch_cb");
    inject_idxcb_dispatcher("dispatch_idxcb");
    inject_ed_dispatcher("dispatch_ed");
  }
};

} // namespace specbolt::v4
