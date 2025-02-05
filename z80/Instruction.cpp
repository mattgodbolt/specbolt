#include "z80/Instruction.hpp"

#include "z80/Alu.hpp"

#include <format>
#include <iostream>
#include <stdexcept>

namespace specbolt {

Instruction::Output Instruction::apply(const Operation operation, const Input &input) {
  Output result{input.dest, input.pc, input.sp, input.flags, input.iff1, input.iff2, 0, input.port_fe};
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

    case Operation::Add16: {
      const auto alu_result = Alu::add16(input.dest, input.source, false);
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;
    case Operation::Add16WithCarry: {
      const auto alu_result = Alu::add16(input.dest, input.source, input.flags.carry());
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;
    case Operation::Compare: {
      // Only update the flags on compare.
      result.flags = Alu::cmp8(static_cast<std::uint8_t>(input.dest), static_cast<std::uint8_t>(input.source)).flags;
    } break;
    case Operation::Subtract8: {
      const auto alu_result =
          Alu::sub8(static_cast<std::uint8_t>(input.dest), static_cast<std::uint8_t>(input.source), false);
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;
    case Operation::Subtract8WithCarry: {
      const auto alu_result = Alu::sub8(
          static_cast<std::uint8_t>(input.dest), static_cast<std::uint8_t>(input.source), input.flags.carry());
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;
    case Operation::Subtract16: {
      const auto alu_result = Alu::sub16(input.dest, input.source, false);
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;
    case Operation::Subtract16WithCarry: {
      const auto alu_result = Alu::sub16(input.dest, input.source, input.flags.carry());
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;
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
    case Operation::Xor: {
      const auto alu_result = Alu::xor8(static_cast<std::uint8_t>(input.dest), static_cast<std::uint8_t>(input.source));
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;
    case Operation::And: {
      const auto alu_result = Alu::and8(static_cast<std::uint8_t>(input.dest), static_cast<std::uint8_t>(input.source));
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;
    case Operation::Or: {
      const auto alu_result = Alu::or8(static_cast<std::uint8_t>(input.dest), static_cast<std::uint8_t>(input.source));
      result.value = alu_result.result;
      result.flags = alu_result.flags;
    } break;
    case Operation::Irq:
      // OR todo "iff1 and 2" destination and load?
      result.iff1 = result.iff2 = input.source;
      break;
    case Operation::Out:
      // TODO starts to smell a bit here...
      if (input.dest == 0xfe)
        result.port_fe = static_cast<std::uint8_t>(input.source);
      else
        std::print(std::cout, "zomg OUT({:02x}, {:02x})\n", input.dest, input.source);
      break;
    case Operation::Exx:
      break;
    case Operation::Invalid:
      // TODO better
      throw std::runtime_error("Unsupported opcode");
  }
  return result;
}

} // namespace specbolt
