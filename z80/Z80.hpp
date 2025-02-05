#pragma once

#include "peripherals/Memory.hpp"
#include "z80/Opcodes.hpp"
#include "z80/RegisterFile.hpp"

#include <cstdint>

namespace specbolt {

struct Instruction;

class Z80 {
public:
  explicit Z80(Memory &memory) : memory_(memory) {}

  // TODO "Cycles" class?
  std::size_t execute_one();

  [[nodiscard]] bool iff1() const { return iff1_; }
  [[nodiscard]] bool iff2() const { return iff2_; }
  [[nodiscard]] std::uint16_t pc() const { return regs_.pc(); }
  // TODO this isn't a property of the z80, we should "poke" out to a port mapper or something.
  [[nodiscard]] std::uint8_t port_fe() const { return port_fe_; }

  void dump() const;

  [[nodiscard]] const auto &regs() const { return regs_; }

private:
  RegisterFile regs_;
  Memory &memory_;
  std::size_t now_tstates_{};
  bool iff1_{};
  bool iff2_{};
  std::uint8_t port_fe_{};

  [[nodiscard]] std::uint16_t read16(std::uint16_t address) const;
  [[nodiscard]] std::uint16_t read(Instruction::Operand operand) const;
  void write(Instruction::Operand operand, std::uint16_t value);
  void write8(std::uint16_t address, std::uint8_t value);
  void write16(std::uint16_t address, std::uint16_t value);
  void pass_time(std::size_t tstates);
  void execute(const Instruction &instr);
};

} // namespace specbolt
