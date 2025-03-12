#pragma once

#ifdef SPECBOLT_MODULES
import peripherals;
#else
#include "peripherals/Memory.hpp"
#endif

#include "z80/common/RegisterFile.hpp"
#include "z80/common/Z80Base.hpp"
#include "z80/v1/Instruction.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace specbolt::v1 {

class Z80 : public Z80Base {
public:
  explicit Z80(Memory &memory) : Z80Base(memory) {}

  // TODO "Cycles" class?
  std::size_t execute_one();

  void push16(std::uint16_t value);
  [[nodiscard]] std::uint16_t pop16();
  void push8(std::uint8_t value);
  [[nodiscard]] std::uint8_t pop8();

  void interrupt();
  void retn();

  void branch(std::int8_t offset);

  [[nodiscard]] std::vector<RegisterFile> history() const;

  [[nodiscard]] std::uint8_t read8(std::uint16_t address) const;
  [[nodiscard]] std::uint16_t read16(std::uint16_t address) const;
  void write8(std::uint16_t address, std::uint8_t value);
  void write16(std::uint16_t address, std::uint16_t value);


  [[nodiscard]] std::uint16_t read(Instruction::Operand operand, std::int8_t index_offset);
  void write(Instruction::Operand operand, std::int8_t index_offset, std::uint16_t value);

private:
  static constexpr std::size_t RegHistory = 8z;
  std::array<RegisterFile, RegHistory> reg_history_{};
  size_t current_reg_history_index_{};

  void execute(const Instruction &instr);
};

} // namespace specbolt::v1
