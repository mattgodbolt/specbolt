#pragma once

#include "peripherals/Memory.hpp"
#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/common/Z80Base.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace specbolt::v2 {

class Z80 : public Z80Base {
public:
  explicit Z80(Memory &memory) : Z80Base(memory) {}

  void execute_one();

  void interrupt();

  void branch(std::int8_t offset);

  [[nodiscard]] std::vector<RegisterFile> history() const;

  std::uint8_t read_immediate();
  std::uint16_t read_immediate16();

private:
  static constexpr std::size_t RegHistory = 8z;
  std::array<RegisterFile, RegHistory> reg_history_{};
  size_t current_reg_history_index_{};
};

} // namespace specbolt::v2
