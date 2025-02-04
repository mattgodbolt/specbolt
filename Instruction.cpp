#include "Instruction.hpp"

#include <format>
#include <iostream>
#include <stdexcept>

#include "Alu.hpp"

namespace specbolt {

Instruction::Output Instruction::apply(const Operation operation, const Input &input) {
  Output result{input.dest, input.pc, input.sp, input.flags, input.iff1, input.iff2, 0};
  // TODO tstates ... like ED executes take longer etc
  switch (operation) {
    case Operation::None:
      break;
    case Operation::Add8: {
      const auto alu_result =
          Alu::add8(static_cast<std::uint8_t>(input.dest), static_cast<std::uint8_t>(input.source), false);
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;
    case Operation::Add8WithCarry: {
      const auto alu_result = Alu::add8(
          static_cast<std::uint8_t>(input.dest), static_cast<std::uint8_t>(input.source), input.flags.carry());
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;

    case Operation::Add16:
    case Operation::Add16WithCarry:
      throw std::runtime_error("todo");
      break;
    case Operation::Subtract8:
    case Operation::Subtract8WithCarry: {
      // const std::uint16_t result = input.dest - input.source;
      result.value = input.dest - input.source; // TODO flags in some cases
    } break;
    case Operation::Subtract16:
      result.value = input.dest - input.source;
      break;
    case Operation::Subtract16WithCarry:
      throw std::runtime_error("todo");
    case Operation::And:
      throw std::runtime_error("todo");
      break;
    case Operation::Or:
      throw std::runtime_error("todo");
      break;
    case Operation::Load:
      result.value = input.source;
      break;
    case Operation::Jump:
      // TODO conditions?
      result.extra_t_states = 6;
      result.pc = input.source;
      break;
    case Operation::Bit:
      break;
    case Operation::Xor:
      result.value = input.dest ^ input.source;
      // TODO flags
      break;
    case Operation::Irq:
      // OR todo "iff1 and 2" destination and load?
      result.iff1 = result.iff2 = input.source;
      break;
    case Operation::Out:
      // YUCK
      std::print(std::cout, "zomg OUT({:02x}, {:02x})\n", input.dest, input.source);
      break;
    case Operation::Invalid:
      // TODO better
      throw std::runtime_error(std::format("Unsupported opcode at {:04x}", input.pc));
  }
  return result;
}

} // namespace specbolt
