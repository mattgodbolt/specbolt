#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/common/Z80Base.hpp"

#include <cstdint>
#include <functional>

#include "peripherals/Memory.hpp"
#endif

namespace specbolt::v4 {

SPECBOLT_EXPORT class Z80 : public Z80Base {
public:
  explicit Z80(Scheduler &scheduler, Memory &memory) : Z80Base(scheduler, memory) {}

  void execute_one();

  std::uint8_t read_opcode();
  std::uint8_t read_immediate();
  std::uint16_t read_immediate16();

  [[nodiscard]] std::uint8_t read(std::uint16_t address);
  void write(std::uint16_t address, std::uint8_t value);

  using Z80Base::halted;
  void halted(bool value);
};

} // namespace specbolt::v4
