#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace specbolt {

class Memory;

class Disassembler {
public:
  explicit Disassembler(Memory &memory) : memory_(memory) {}

  struct Instruction {
    uint16_t address{};
    uint8_t length{};
    std::array<uint8_t, 4> bytes{};
    bool operator==(const Instruction &other) const = default;
    std::string to_string() const;
    // TODO std::format specialisation
  };

  Instruction disassemble(std::uint16_t address) const;

private:
  Memory &memory_;
};

} // namespace specbolt
