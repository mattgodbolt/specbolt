#pragma once

#include "RegisterFile.hpp"

#include <array>
#include <cstdint>

namespace specbolt {

class Z80 {
public:
  explicit Z80(std::array<std::uint8_t, 65536> &memory) : memory_(memory) {}

  void execute_one();


private:
  RegisterFile regs_;
  std::array<std::uint8_t, 65536> &memory_;
};

} // namespace specbolt
