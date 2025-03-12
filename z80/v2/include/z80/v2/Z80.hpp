#pragma once

#ifdef SPECBOLT_MODULES
import peripherals;
#else
#include "peripherals/Memory.hpp"
#endif
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

  void write(std::uint16_t address, std::uint8_t byte);
  std::uint8_t read(std::uint16_t address);
  std::uint8_t pop8();
  std::uint16_t pop16();
  void push8(std::uint8_t value);
  void push16(std::uint16_t value);

private:
  static constexpr std::size_t RegHistory = 8z;
  std::array<RegisterFile, RegHistory> reg_history_{};
  size_t current_reg_history_index_{};
};

} // namespace specbolt::v2
