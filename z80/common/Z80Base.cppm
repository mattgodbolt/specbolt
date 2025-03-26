module;

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <vector>

export module z80_common:Z80Base;

import peripherals;
import :Flags;
import :RegisterFile;
import :Scheduler;


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


namespace specbolt {

Flags Z80Base::flags() const { return Flags(regs_.get(RegisterFile::R8::F)); }
void Z80Base::flags(const Flags flags) { regs_.set(RegisterFile::R8::F, flags.to_u8()); }

void Z80Base::halt() {
  halted_ = true;
  regs().pc(regs().pc() - 1);
}

void Z80Base::add_out_handler(OutHandler handler) { out_handlers_.emplace_back(std::move(handler)); }
void Z80Base::add_in_handler(InHandler handler) { in_handlers_.emplace_back(std::move(handler)); }

void Z80Base::out(const std::uint16_t port, const std::uint8_t value) {
  for (const auto &handler: out_handlers_)
    handler(port, value);
}

std::uint8_t Z80Base::in(const std::uint16_t port) {
  std::uint8_t combined_result = 0xff;
  for (const auto &handler: in_handlers_) {
    if (const auto result = handler(port); result.has_value())
      combined_result &= *result;
  }
  return combined_result;
}

void Z80Base::dump() const {
  regs_.dump(std::cout, "");
  std::print(std::cout, "ROM flags : {}, {}, {}, {}\n", memory_.rom_flags()[0], memory_.rom_flags()[1],
      memory_.rom_flags()[2], memory_.rom_flags()[3]);
  std::print(std::cout, "Page      : {}, {}, {}, {}\n", memory_.page_table()[0], memory_.page_table()[1],
      memory_.page_table()[2], memory_.page_table()[3]);
}

} // namespace specbolt
