#pragma once

#include "RegisterFile.hpp"

#include "Memory.hpp"

#include <cstdint>

#include "Opcodes.hpp"

namespace specbolt {

struct Instruction;

class Z80 {
public:
  explicit Z80(Memory &memory) : memory_(memory) {}

  void execute_one();

private:
  RegisterFile regs_;
  Memory &memory_;
  std::size_t now_tstates_{};
  bool iff1_{};
  bool iff2_{};

  [[nodiscard]] std::uint8_t read_and_inc_pc();
  [[nodiscard]] std::uint16_t read16_and_inc_pc();
  [[nodiscard]] std::uint16_t read16(std::uint16_t address) const;
  [[nodiscard]] std::uint16_t read(Instruction::Operand operand) const;
  [[nodiscard]] std::uint16_t apply(Instruction::Operation operation, std::uint16_t dest, std::uint16_t source);
  void write(Instruction::Operand operand, std::uint16_t value);
  void pass_time(std::size_t tstates);
  void execute(const Instruction &instr);
};

} // namespace specbolt
