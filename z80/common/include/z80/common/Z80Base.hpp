#pragma once

#ifndef SPECBOLT_MODULES
#include "peripherals/Memory.hpp"
#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/common/Scheduler.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
#endif

namespace specbolt {

// Non-virtual base class for Z80 implementations.
export class Z80Base {
public:
  explicit Z80Base(Scheduler &scheduler, Memory &memory) : scheduler_(scheduler), memory_(memory) {}

  [[nodiscard]] bool iff1() const { return iff1_; }
  void iff1(const bool iff1) { iff1_ = iff1; }
  [[nodiscard]] bool iff2() const { return iff2_; }
  void iff2(const bool iff2) { iff2_ = iff2; }
  [[nodiscard]] std::uint16_t pc() const { return regs_.pc(); }
  [[nodiscard]] auto &regs(this auto &&self) { return self.regs_; }
  [[nodiscard]] auto &memory(this auto &self) { return self.memory_; }

  void irq_mode(const std::uint8_t mode) { irq_mode_ = mode; }
  [[nodiscard]] std::uint8_t irq_mode() const { return irq_mode_; };
  void interrupt() { irq_pending_ = true; }

  [[nodiscard]] Flags flags() const;
  void flags(Flags flags);

  [[nodiscard]] auto cycle_count() const { return scheduler_.cycles(); }

  using OutHandler = std::function<void(std::uint16_t port, std::uint8_t value)>;
  void add_out_handler(OutHandler handler);
  void out(std::uint16_t port, std::uint8_t value);
  std::uint8_t in(std::uint16_t port);

  using InHandler = std::function<std::optional<std::uint8_t>(std::uint16_t port)>;
  void add_in_handler(InHandler handler);

  void dump() const;

  void halt();
  [[nodiscard]] bool halted() const { return halted_; }

  void pass_time(const std::size_t tstates) { scheduler_.tick(tstates); }

protected:
  RegisterFile regs_;
  Scheduler &scheduler_;
  Memory &memory_;
  bool halted_{};
  bool irq_pending_{};
  bool iff1_{};
  bool iff2_{};
  std::uint8_t irq_mode_{};
  std::vector<InHandler> in_handlers_;
  std::vector<OutHandler> out_handlers_;
};

} // namespace specbolt
