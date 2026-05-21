#pragma once

#include "peripherals/Memory.hpp"
#include "z80/common/Z80Base.hpp"
#include "z80/v4/Instructions.hpp"
#include "z80/v4/Shapes.hpp"

#include <cstdint>
#include <meta>

namespace specbolt::v4 {

class Z80 : public Z80Base {
public:
  explicit Z80(Scheduler &scheduler, Memory &memory) : Z80Base(scheduler, memory) {}

  void execute_one();

  std::uint8_t read_immediate();
  std::uint16_t read_immediate16();

  std::uint8_t read(std::uint16_t address);
  void write(std::uint16_t address, std::uint8_t byte);

  // ----------------------------------------------------------------------
  // Reflection-driven dispatch: build the `dispatch_base(uint8_t)` switch by
  // (a) emitting one arm per `InsnDef` in `base_instructions` and
  // (b) letting each shape generator emit its expanded arms (e.g. emit_ld_rr
  // emits 63 arms for `ld r, r'` from one nested loop).
  consteval {
    std::meta::list_builder cases;

    // Individual opcodes from the declarative table.
    for (auto const &i : base_instructions) {
      cases += ^^{ case \(i.opcode): \(i.body) break; };
    }

    // Parameterised shapes.
    emit_ld_rr(cases);

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
