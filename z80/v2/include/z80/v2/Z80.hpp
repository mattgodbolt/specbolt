#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/common/Z80Base.hpp"

#include <cstdint>
#include <functional>

#include "peripherals/Memory.hpp"
#endif

namespace specbolt::v2 {

SPECBOLT_EXPORT class Z80 : public Z80Base {
public:
  explicit Z80(Memory &memory) : Z80Base(memory) {}

  std::size_t execute_one();

  void interrupt();

  void branch(std::int8_t offset);

  std::uint8_t read_opcode();
  std::uint8_t read_immediate();
  std::uint16_t read_immediate16();

  void write(std::uint16_t address, std::uint8_t byte);
  std::uint8_t read(std::uint16_t address);
  std::uint8_t pop8();
  std::uint16_t pop16();
  void push8(std::uint8_t value);
  void push16(std::uint16_t value);
};

} // namespace specbolt::v2
