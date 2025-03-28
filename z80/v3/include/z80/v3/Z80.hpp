#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/common/Z80Base.hpp"

#include <cstdint>
#include <functional>

#include "peripherals/Memory.hpp"
#endif

namespace specbolt::v3 {

SPECBOLT_EXPORT class Z80 : public Z80Base {
public:
  explicit Z80(Scheduler &scheduler, Memory &memory) : Z80Base(scheduler, memory) {}

  void execute_one();

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

private:
  void handle_interrupt();

  [[nodiscard]] bool check_nz() const;
  [[nodiscard]] bool check_z() const;
  [[nodiscard]] bool check_nc() const;
  [[nodiscard]] bool check_c() const;
  [[nodiscard]] bool check_po() const;
  [[nodiscard]] bool check_pe() const;
  [[nodiscard]] bool check_p() const;
  [[nodiscard]] bool check_m() const;

  // Implementation is in the generated code
  void execute_one_base();
};

} // namespace specbolt::v3
