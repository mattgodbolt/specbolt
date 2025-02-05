#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "z80/Instruction.hpp"

namespace specbolt {

class Memory;

class Disassembler {
public:
  explicit Disassembler(Memory &memory) : memory_(memory) {}

  struct Disassembled {
    Instruction instruction;
    uint16_t address{};
    std::array<uint8_t, 4> bytes{};
    // bool operator==(const Disassembled &other) const = default;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string operand_name(Instruction::Operand operand) const;
  };

  [[nodiscard]] Disassembled disassemble(std::uint16_t address) const;

private:
  Memory &memory_;
};

} // namespace specbolt
