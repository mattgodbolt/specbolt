#pragma once

#include "peripherals/Memory.hpp"
#include "z80/Decoder.hpp"
#include "z80/RegisterFile.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace specbolt {

struct Instruction;

class Z80 {
public:
  explicit Z80(Memory &memory) : memory_(memory) {}

  // TODO "Cycles" class?
  std::size_t execute_one();

  [[nodiscard]] bool iff1() const { return iff1_; }
  void iff1(const bool iff1) { iff1_ = iff1; }
  [[nodiscard]] bool iff2() const { return iff2_; }
  void iff2(const bool iff2) { iff2_ = iff2; }
  [[nodiscard]] std::uint16_t pc() const { return regs_.pc(); }
  [[nodiscard]] const auto &registers() const { return regs_; }
  auto &registers() { return regs_; }
  [[nodiscard]] const auto &memory() const { return memory_; }
  auto &memory() { return memory_; }

  void halt();
  [[nodiscard]] bool halted() const { return halted_; }

  void irq_mode(std::uint8_t mode);

  void push16(std::uint16_t value);
  [[nodiscard]] std::uint16_t pop16();
  void push8(std::uint8_t value);
  [[nodiscard]] std::uint8_t pop8();

  using OutHandler = std::function<void(std::uint16_t port, std::uint8_t value)>;
  void add_out_handler(OutHandler handler);
  void out(std::uint16_t port, std::uint8_t value);
  std::uint8_t in(std::uint16_t port);

  using InHandler = std::function<std::optional<uint8_t>(std::uint16_t port)>;
  void add_in_handler(InHandler handler);

  void interrupt();
  void retn();

  void dump() const;

  [[nodiscard]] const auto &regs() const { return regs_; }
  [[nodiscard]] std::uint8_t read8(std::uint16_t address) const;
  [[nodiscard]] std::uint16_t read16(std::uint16_t address) const;
  void write8(std::uint16_t address, std::uint8_t value);
  void write16(std::uint16_t address, std::uint16_t value);

  [[nodiscard]] auto num_instructions_executed() const { return num_instructions_; }
  [[nodiscard]] auto cycle_count() const { return now_tstates_; }

  [[nodiscard]] std::vector<RegisterFile> history() const;

  [[nodiscard]] std::uint16_t read(Instruction::Operand operand, std::int8_t index_offset);
  void write(Instruction::Operand operand, std::int8_t index_offset, std::uint16_t value);

private:
  RegisterFile regs_;
  Memory &memory_;
  std::size_t num_instructions_{};
  std::size_t now_tstates_{};
  bool halted_{};
  bool iff1_{};
  bool iff2_{};
  std::uint8_t irq_mode_{};
  static constexpr std::size_t RegHistory = 8z;
  std::array<RegisterFile, RegHistory> reg_history_{};
  size_t current_reg_history_index_{};
  std::vector<InHandler> in_handlers_;
  std::vector<OutHandler> out_handlers_;

  void pass_time(std::size_t tstates);
  void execute(const Instruction &instr);
};

} // namespace specbolt
