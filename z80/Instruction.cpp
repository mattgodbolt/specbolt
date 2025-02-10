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

bool Instruction::should_execute(const Flags flags) const {
  if (const auto *condition = std::get_if<Condition>(&args)) {
    switch (*condition) {
      case Condition::NonZero:
        return !flags.zero();
      case Condition::Zero:
        return flags.zero();
      case Condition::NoCarry:
        return !flags.carry();
      case Condition::Carry:
        return flags.carry();
    }
  }
  return true;
}

Instruction::Output Instruction::apply(const Input input, Z80 &cpu) const {
  // TODO tstates ... like ED executes take longer etc
  const bool carry = std::holds_alternative<WithCarry>(args) ? input.flags.carry() : false;
  switch (operation) {
    case Operation::None:
      return {0, Flags(), 0};

    case Operation::Add8: {
      const auto [result, flags] =
          Alu::add8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs), carry);
      return {result, flags, 0};
    }

    case Operation::Add16: {
      const auto [result, flags] = Alu::add16(input.lhs, input.rhs, carry);
      return {result, flags, 7};
    }
    case Operation::Add16NoFlags: {
      return {static_cast<std::uint16_t>(input.lhs + input.rhs), input.flags, 2};
    }
    case Operation::Compare:
      // Only update the flags on compare.
      return {0, Alu::cmp8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs)).flags, 0};
    case Operation::Subtract8: {
      const auto [result, flags] =
          Alu::sub8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs), carry);
      return {result, flags, 0};
    }
    case Operation::Subtract16: {
      const auto [result, flags] = Alu::sub16(input.lhs, input.rhs, carry);
      return {result, flags, 7};
    }

    case Operation::Load:
      return {input.rhs, input.flags, 0}; // TODO is 0 right?
    case Operation::Jump: {
      const auto taken = should_execute(input.flags);
      if (taken)
        cpu.registers().pc(input.rhs);
      // TODO t-states not right for jp vs jr
      return {0, input.flags, static_cast<std::uint8_t>(taken ? 6 : 3)};
    }
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
    case Operation::IrqMode:
      cpu.irq_mode(static_cast<std::uint8_t>(input.rhs));
      return {0, input.flags, 4};
    case Operation::Out:
      cpu.out(input.lhs, static_cast<std::uint8_t>(input.rhs));
      return {0, input.flags, 7};
    case Operation::Exx:
      cpu.registers().exx();
      return {0, input.flags, 0};
    case Operation::Exchange: {
      switch (lhs) {
        case Operand::AF:
          cpu.registers().ex(RegisterFile::R16::AF, RegisterFile::R16::AF_);
          break;
        case Operand::DE:
          cpu.registers().ex(RegisterFile::R16::DE, RegisterFile::R16::HL);
          break;
        default:
          throw std::runtime_error("Unsupported exchange");
      }
      return {0, input.flags, 0};
    }
    case Operation::EdOp: {
      const auto [op, increment, repeat] = std::get<EdOpArgs>(args);
      const std::uint16_t add = increment ? 0x0001 : 0xffff;
      const auto hl = cpu.registers().get(RegisterFile::R16::HL);
      cpu.registers().set(RegisterFile::R16::HL, hl + add);
      const auto bc = cpu.registers().get(RegisterFile::R16::BC);
      bool should_continue = bc != 0 && repeat;
      cpu.registers().set(RegisterFile::R16::BC, bc - 1);
      auto flags = input.flags & ~(Flags::Subtract() | Flags::HalfCarry() | Flags::Overflow());
      if (bc == 1)
        flags = flags | Flags::Overflow();

      switch (op) {
        case EdOpArgs::Op::Load: {
          const auto de = cpu.registers().get(RegisterFile::R16::DE);
          cpu.registers().set(RegisterFile::R16::DE, de + add);
          const auto byte = cpu.memory().read(hl);
          cpu.memory().write(de, byte);
          break;
        }
        case EdOpArgs::Op::Compare: {
          const auto byte = cpu.memory().read(hl);
          const auto compare_flags = Alu::cmp8(cpu.registers().get(RegisterFile::R8::A), byte).flags;
          constexpr auto flags_to_copy = Flags::HalfCarry() | Flags::Zero() | Flags::Sign();
          flags = flags & ~flags_to_copy | (compare_flags & flags_to_copy);
          if (flags.zero())
            should_continue = false;
          break;
        }
        case EdOpArgs::Op::In:
          throw std::runtime_error("in not supported yet TODO");
        case EdOpArgs::Op::Out:
          throw std::runtime_error("out not supported yet TODO");
      }
      if (should_continue) {
        cpu.registers().pc(cpu.registers().pc() - 2);
        return {0, flags, 21};
      }
      return {0, flags, 12};
    }
    case Operation::Set:
      return {static_cast<std::uint16_t>(input.lhs | static_cast<std::uint16_t>(1u << input.rhs)), input.flags, 4};
    case Operation::Reset:
      return {static_cast<std::uint16_t>(input.lhs & ~static_cast<std::uint16_t>(1u << input.rhs)), input.flags, 4};

    case Operation::Shift:
    case Operation::Bit:
    case Operation::Invalid:
      break;
  }
  // TODO better
  throw std::runtime_error("Unsupported opcode");
}

} // namespace specbolt
