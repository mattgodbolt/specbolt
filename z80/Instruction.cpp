#include "z80/Instruction.hpp"

#include "z80/Alu.hpp"
#include "z80/Z80.hpp"

#include <stdexcept>

namespace specbolt {

namespace {
Instruction::Output alu8(const Instruction::Input input, Alu::R8 (*operation)(std::uint8_t, std::uint8_t)) {
  const auto [result, flags] = operation(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs));
  return {result, flags, 0};
}
} // namespace

Instruction::Output Instruction::apply(Input input, Z80 &cpu) const {
  // TODO tstates ... like ED executes take longer etc
  const bool carry = with_carry ? input.flags.carry() : false;
  const bool taken = condition == Condition::None || (condition == Condition::Zero && input.flags.zero()) ||
                     (condition == Condition::NonZero && !input.flags.zero()) ||
                     (condition == Condition::Carry && input.flags.carry()) ||
                     (condition == Condition::NoCarry && !input.flags.carry());
  switch (operation) {
    case Operation::None:
      return {0, Flags(), 0};

    case Operation::Add8: {
      const auto [result, flags] =
          Alu::add8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs), carry);
      return {result, flags, 0};
    } break;

    case Operation::Add16: {
      const auto [result, flags] = Alu::add16(input.lhs, input.rhs, carry);
      return {result, flags, 7}; //
    } break;
    case Operation::Compare:
      // Only update the flags on compare.
      return {0, Alu::cmp8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs)).flags, 0};
      break;
    case Operation::Subtract8: {
      const auto [result, flags] =
          Alu::sub8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs), carry);
      return {result, flags, 0};
    } break;
    case Operation::Subtract16: {
      const auto [result, flags] = Alu::sub16(input.lhs, input.rhs, carry);
      return {result, flags, 7};
    } break;

    case Operation::Load:
      return {input.lhs, input.flags, 0}; // TODO is 0 right?
      break;
    case Operation::Jump:
      if (taken)
        cpu.registers().pc(input.rhs);
      // TODO t-states not right for jp vs jr
      return {0, input.flags, static_cast<std::uint8_t>(taken ? 6 : 3)};
      break;
    case Operation::Bit:
      break;
    case Operation::Xor:
      return alu8(input, &Alu::xor8);
    case Operation::And:
      return alu8(input, &Alu::and8);
    case Operation::Or:
      return alu8(input, &Alu::or8);
    case Operation::Irq:
      cpu.iff1(input.rhs);
      cpu.iff2(input.rhs);
      return {0, input.flags, 0};
    case Operation::Out:
      cpu.out(input.lhs, static_cast<std::uint8_t>(input.rhs));
      return {0, input.flags, 7};
    case Operation::Exx:
      cpu.registers().exx();
      return {0, input.flags, 0};
    case Operation::Exchange:
      throw std::runtime_error("Exchange is baddd");
    case Operation::Invalid:
      break;
  }
  // TODO better
  throw std::runtime_error("Unsupported opcode");
}

} // namespace specbolt
