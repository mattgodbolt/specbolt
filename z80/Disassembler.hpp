#pragma once

#ifndef SPECBOLT_IN_MODULE
#include <array>
#include <cstdint>
#include <string>
#endif

#include "module.hpp"
#include "Instruction.hpp"


namespace specbolt {

#ifndef SPECBOLT_IN_MODULE
class Memory;
#endif

SPECBOLT_EXPORT class Disassembler {
public:
  explicit Disassembler(const Memory &memory) : memory_(memory) {}

  struct Disassembled {
    Instruction instruction;
    uint16_t address{};
    std::array<uint8_t, 4> bytes{};
    // bool operator==(const Disassembled &other) const = default;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string operand_name(Instruction::Operand operand, std::int8_t offset) const;
  };

  [[nodiscard]] Disassembled disassemble(std::uint16_t address) const;

private:
  const Memory &memory_;
};

} // namespace specbolt
