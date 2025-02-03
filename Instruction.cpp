#include "Instruction.hpp"

#include <format>
#include <stdexcept>

namespace specbolt {

Instruction::Output Instruction::apply(Operation operation, const Input &input) {
  Output result{input.dest, input.pc, input.sp, input.flags};
  switch (operation) {
    case Operation::None:
      break;
    case Operation::Add:
      // Consider bit width?
      break;
    case Operation::Subtract:
      // Consider bit width?
      // BUT ALSO ARGH FLAGS FOR DEC BC etc unaffected
      break;
    case Operation::Load:
      result.value = input.source;
      break;
    case Operation::Jump:
      break;
    case Operation::Bit:
      break;
    case Operation::Xor:
      result.value = input.dest ^ input.source;
      // TODO flags
      break;
    case Operation::Irq:
      // TODO iff1 and iff2
      break;
    case Operation::Invalid:
      // TODO better
      throw std::runtime_error(std::format("Unsupported opcode at {:04x}", input.pc));
  }
  return result;
}

} // namespace specbolt
