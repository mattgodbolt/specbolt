#pragma once

#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace specbolt {

class Memory;

// Non-virtual base class for Z80 implementations.
class Z80Base {
public:
  explicit Z80Base(Memory &memory) : memory_(memory) {}

  [[nodiscard]] bool iff1() const { return iff1_; }
  void iff1(const bool iff1) { iff1_ = iff1; }
  [[nodiscard]] bool iff2() const { return iff2_; }
  void iff2(const bool iff2) { iff2_ = iff2; }
  [[nodiscard]] std::uint16_t pc() const { return regs_.pc(); }
  [[nodiscard]] const auto &registers() const { return regs_; }
  auto &registers() { return regs_; } // TODO omg remove
  [[nodiscard]] const auto &memory() const { return memory_; }
  auto &memory() { return memory_; }

  void irq_mode(const std::uint8_t mode) { irq_mode_ = mode; }
  [[nodiscard]] std::uint8_t irq_mode() const { return irq_mode_; };

  [[nodiscard]] auto &regs() { return regs_; } // TODO omg remove
  [[nodiscard]] Flags flags() const;
  void flags(Flags flags);

  [[nodiscard]] auto num_instructions_executed() const { return num_instructions_; }
  [[nodiscard]] auto cycle_count() const { return now_tstates_; }

  using OutHandler = std::function<void(std::uint16_t port, std::uint8_t value)>;
  void add_out_handler(OutHandler handler);
  void out(std::uint16_t port, std::uint8_t value);
  std::uint8_t in(std::uint16_t port);

  using InHandler = std::function<std::optional<uint8_t>(std::uint16_t port)>;
  void add_in_handler(InHandler handler);

  void dump() const;

  void halt();
  [[nodiscard]] bool halted() const { return halted_; }

  void pass_time(const size_t tstates) { now_tstates_ += tstates; }

protected:
  RegisterFile regs_;
  Memory &memory_;
  std::size_t num_instructions_{};
  std::size_t now_tstates_{};
  bool halted_{};
  bool iff1_{};
  bool iff2_{};
  std::uint8_t irq_mode_{};
  std::vector<InHandler> in_handlers_;
  std::vector<OutHandler> out_handlers_;
};

} // namespace specbolt
