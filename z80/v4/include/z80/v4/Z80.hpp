#pragma once

#include "peripherals/Memory.hpp"
#include "z80/common/Z80Base.hpp"
#include "z80/v4/Instructions.hpp"

#include <cstdint>
#include <meta>

namespace specbolt::v4 {

class Z80 : public Z80Base {
public:
  explicit Z80(Scheduler &scheduler, Memory &memory) : Z80Base(scheduler, memory) {}

  void execute_one();

  std::uint8_t read_immediate();
  std::uint16_t read_immediate16();

  // ----------------------------------------------------------------------
  // Reflection-driven dispatch: walk the instruction table at consteval
  // time and inject a `dispatch_base(uint8_t)` member whose body is a
  // switch over every opcode, each case carrying the stored token sequence.
  consteval {
    std::meta::list_builder cases;
    for (auto const &i : base_instructions) {
      cases += ^^{
        case \(i.opcode):
          \(i.body)
          break;
      };
    }
    std::meta::queue_injection(^^{
      void dispatch_base(std::uint8_t opcode) {
        switch (opcode) {
          \(cases)
          default: break; // TODO: unimplemented opcode
        }
      }
    });
  }
};

} // namespace specbolt::v4
