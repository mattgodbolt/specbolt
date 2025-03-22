#pragma once


#ifndef SPECBOLT_MODULES
#include <array>
#include <cstdint>
#include <vector>
#include "peripherals/Memory.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/common/Z80Base.hpp"
#include "z80/v1/Instruction.hpp"
#endif

namespace specbolt::v1 {

SPECBOLT_EXPORT class Z80 : public Z80Base {
public:
  explicit Z80(Scheduler &scheduler, Memory &memory) : Z80Base(scheduler, memory) {}

  void execute_one();

  void push16(std::uint16_t value);
  [[nodiscard]] std::uint16_t pop16();
  void push8(std::uint8_t value);
  [[nodiscard]] std::uint8_t pop8();

  void retn();

  void branch(std::int8_t offset);

  [[nodiscard]] std::uint8_t read8(std::uint16_t address) const;
  [[nodiscard]] std::uint16_t read16(std::uint16_t address) const;
  void write8(std::uint16_t address, std::uint8_t value);
  void write16(std::uint16_t address, std::uint16_t value);

  [[nodiscard]] std::uint16_t read(Instruction::Operand operand, std::int8_t index_offset);
  void write(Instruction::Operand operand, std::int8_t index_offset, std::uint16_t value);

private:
  void execute(const Instruction &instr);
  void handle_interrupt();
};

} // namespace specbolt::v1
