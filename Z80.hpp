#pragma once

#include "RegisterFile.hpp"

#include "Memory.hpp"

#include <cstdint>

namespace specbolt {

class Z80 {
public:
  explicit Z80(Memory &memory) : memory_(memory) {}

  void execute_one();

private:
  RegisterFile regs_;
  Memory &memory_;
  std::size_t now_tstates_{};

  [[nodiscard]] std::uint8_t read_and_inc_pc();
  [[nodiscard]] std::uint16_t read16_and_inc_pc();
  void pass_time(std::size_t tstates);
};

} // namespace specbolt
