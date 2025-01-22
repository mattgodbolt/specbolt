#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace specbolt {

class Disassembler {
public:
  explicit Disassembler(std::array<std::uint8_t, 65536> &memory) : memory_(memory) {}

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
  std::array<std::uint8_t, 65536> &memory_;
};

} // namespace specbolt
