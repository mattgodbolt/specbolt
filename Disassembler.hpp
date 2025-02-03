#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace specbolt {

class Memory;
struct Instruction;

class Disassembler {
public:
  explicit Disassembler(Memory &memory) : memory_(memory) {}

  struct Disassembled {
    const Instruction &instruction;
    uint16_t address{};
    std::array<uint8_t, 4> bytes{};
    // bool operator==(const Disassembled &other) const = default;
    [[nodiscard]] std::string to_string() const;
    // TODO std::format specialisation
    [[nodiscard]] std::uint16_t immediate_operand() const;
  };

  [[nodiscard]] Disassembled disassemble(std::uint16_t address) const;

private:
  Memory &memory_;
};

} // namespace specbolt
