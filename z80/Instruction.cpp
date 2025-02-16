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
      case Condition::NonZero: return !flags.zero();
      case Condition::Zero: return flags.zero();
      case Condition::NoCarry: return !flags.carry();
      case Condition::Carry: return flags.carry();
      case Condition::Parity: return flags.parity();
      case Condition::NoParity: return !flags.parity();
      case Condition::Positive: return !flags.sign();
      case Condition::Negative: return flags.sign();
    }
  }
  return true;
}

Instruction::Output Instruction::apply(const Input input, Z80 &cpu) const {
  // TODO tstates ... like ED executes take longer etc
  const bool carry = std::holds_alternative<WithCarry>(args) ? input.flags.carry() : false;
  switch (operation) {
    case Operation::None: return {0, input.flags, 0};

    case Operation::Add8: {
      const auto [result, flags] =
          Alu::add8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs), carry);
      return {result, flags, 0};
    }

    case Operation::Add16: {
      const auto [result, flags] = std::holds_alternative<WithCarry>(args)
                                       ? Alu::adc16(input.lhs, input.rhs, carry)
                                       : Alu::add16(input.lhs, input.rhs, input.flags);
      return {result, flags, 7};
    }
    case Operation::Add16NoFlags: {
      return {static_cast<std::uint16_t>(input.lhs + input.rhs), input.flags, 2};
    }
    case Operation::Compare:
      return {
          input.lhs, Alu::cmp8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs)).flags, 0};
    case Operation::Subtract8: {
      const auto [result, flags] =
          Alu::sub8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs), carry);
      return {result, flags, 0};
    }
    case Operation::Subtract16: {
      const auto [result, flags] = std::holds_alternative<WithCarry>(args)
                                       ? Alu::sbc16(input.lhs, input.rhs, carry)
                                       : Alu::sub16(input.lhs, input.rhs, input.flags);
      return {result, flags, 7};
    }

    case Operation::Inc8: {
      const auto [result, flags] = Alu::inc8(static_cast<std::uint8_t>(input.lhs), input.flags);
      return {result, flags, 0};
    }
    case Operation::Dec8: {
      const auto [result, flags] = Alu::dec8(static_cast<std::uint8_t>(input.lhs), input.flags);
      return {result, flags, 0};
    }

    case Operation::Load: return {input.rhs, input.flags, 0}; // TODO is 0 right?
    case Operation::Jump: {
      const auto taken = should_execute(input.flags);
      if (taken)
        cpu.registers().pc(input.rhs);
      // TODO t-states not right for jp vs jr
      return {0, input.flags, static_cast<std::uint8_t>(taken ? 6 : 3)};
    }
    case Operation::Call: {
      const auto taken = should_execute(input.flags);
      if (taken) {
        cpu.push16(cpu.registers().pc());
        cpu.registers().pc(input.rhs);
      }
      return {0, input.flags, static_cast<std::uint8_t>(taken ? 17 : 10)};
    }
    case Operation::Return: {
      const auto taken = should_execute(input.flags);
      if (taken) {
        cpu.registers().pc(cpu.pop16());
      }
      // timings not actually right here.
      return {0, input.flags, static_cast<std::uint8_t>(taken ? 6 : 1)};
    }
    case Operation::Xor: return alu8(input, &Alu::xor8);
    case Operation::And: return alu8(input, &Alu::and8);
    case Operation::Or: return alu8(input, &Alu::or8);
    case Operation::Irq:
      cpu.iff1(input.rhs);
      cpu.iff2(input.rhs);
      return {0, input.flags, 0};
    case Operation::IrqMode: cpu.irq_mode(static_cast<std::uint8_t>(input.rhs)); return {0, input.flags, 4};
    case Operation::Out: cpu.out(input.lhs, static_cast<std::uint8_t>(input.rhs)); return {0, input.flags, 7};
    case Operation::Exx: cpu.registers().exx(); return {0, input.flags, 0};
    case Operation::Exchange: {
      switch (lhs) {
        // have to return the very thing we just swapped. Could return std::nullopt?
        case Operand::AF: {
          cpu.registers().ex(RegisterFile::R16::AF, RegisterFile::R16::AF_);
          return {cpu.registers().get(RegisterFile::R16::AF), input.flags, 0};
        }
        case Operand::DE: {
          cpu.registers().ex(RegisterFile::R16::DE, RegisterFile::R16::HL);
          return {cpu.registers().get(RegisterFile::R16::DE), input.flags, 0};
        }
        case Operand::SP_Indirect16: {
          const auto sp16 = cpu.read16(cpu.registers().sp());
          const auto hl = cpu.registers().get(RegisterFile::R16::HL);
          cpu.write16(cpu.registers().sp(), hl);
          cpu.registers().set(RegisterFile::R16::HL, sp16);
          return {hl, input.flags, 15}; // TODO is this really the right thing to return?
        }
        default: throw std::runtime_error("Unsupported exchange");
      }
    }
    case Operation::EdOp: {
      const auto [op, increment, repeat] = std::get<EdOpArgs>(args);
      const std::uint16_t add = increment ? 0x0001 : 0xffff;
      const auto hl = cpu.registers().get(RegisterFile::R16::HL);
      cpu.registers().set(RegisterFile::R16::HL, hl + add);
      const auto bc = cpu.registers().get(RegisterFile::R16::BC);
      bool should_continue = bc != 1 && repeat;
      cpu.registers().set(RegisterFile::R16::BC, bc - 1);
      auto flags = input.flags & ~(Flags::Subtract() | Flags::HalfCarry() | Flags::Overflow());
      if (bc != 1)
        flags = flags | Flags::Overflow();

      switch (op) {
        case EdOpArgs::Op::Load: {
          const auto de = cpu.registers().get(RegisterFile::R16::DE);
          cpu.registers().set(RegisterFile::R16::DE, de + add);
          const auto byte = cpu.memory().read(hl);
          // bits 3 and 5 come from the weird value of "byte read + A", where bit 3 goes to flag 5, and bit 1 to flag 3.
          const auto flag_bits = static_cast<std::uint8_t>(byte + cpu.registers().get(RegisterFile::R8::A));
          flags = flags & ~(Flags::Flag3() | Flags::Flag5()) | //
                  (flag_bits & 0x08 ? Flags::Flag3() : Flags()) | //
                  (flag_bits & 0x02 ? Flags::Flag5() : Flags());
          cpu.memory().write(de, byte);
          break;
        }
        case EdOpArgs::Op::Compare: {
          const auto byte = cpu.memory().read(hl);
          const auto compare_flags = Alu::cmp8(cpu.registers().get(RegisterFile::R8::A), byte).flags;
          // TODO: flag 3 and flag 5 are supposedly set from "HL - A"'s left over bits?!
#if 0 // TODO test this,
      // bits 3 and 5 come from the weird value of "byte read + A", where bit 3 goes to flag 5, and bit 1 to flag 3.
          const auto flag_bits = static_cast<std::uint8_t>(byte - cpu.registers().get(RegisterFile::R8::A));
          flags = flags & ~(Flags::Flag3() | Flags::Flag5()) | //
                  (flag_bits & 0x08 ? Flags::Flag3() : Flags()) | //
                  (flag_bits & 0x02 ? Flags::Flag5() : Flags());
#endif
          constexpr auto flags_to_copy = Flags::HalfCarry() | Flags::Zero() | Flags::Sign();
          flags = flags & ~flags_to_copy | (compare_flags & flags_to_copy);
          if (flags.zero())
            should_continue = false;
          break;
        }
        case EdOpArgs::Op::In: throw std::runtime_error("in not supported yet TODO");
        case EdOpArgs::Op::Out: throw std::runtime_error("out not supported yet TODO");
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

    case Operation::Djnz: {
      const auto new_b = static_cast<std::uint8_t>(cpu.registers().get(RegisterFile::R8::B) - 1);
      cpu.registers().set(RegisterFile::R8::B, new_b);
      const auto taken = new_b != 0;
      if (taken)
        cpu.registers().pc(input.rhs);
      return {0, input.flags, static_cast<std::uint8_t>(taken ? 9 : 4)};
    }

    case Operation::Bit: {
      const auto bit = static_cast<std::uint8_t>(1 << static_cast<std::uint8_t>(input.rhs));
      const auto flags_persisted = input.flags & Flags::Carry();
      // From js speccy, bit 3 and 5 are copied to flags 3 and 5.
      const auto flags_from_value = Flags(static_cast<std::uint8_t>(input.lhs)) & (Flags::Flag3() | Flags::Flag5());
      const auto flags_from_sign = (bit & input.lhs & 0x80) ? Flags::Sign() : Flags();
      const auto flags_from_bit = input.lhs & bit ? Flags() : (Flags::Zero() | Flags::Parity());
      const auto flags = flags_persisted | flags_from_value | flags_from_sign | flags_from_bit;
      return {0, flags, 4};
    }

    case Operation::Pop: return {cpu.pop16(), input.flags, 6};
    case Operation::Ccf: return {0, input.flags ^ Flags::Carry(), 0};
    case Operation::Scf: return {0, input.flags | Flags::Carry(), 0};
    case Operation::Neg: {
      const auto [result, flags] = Alu::sub8(0, static_cast<std::uint8_t>(input.lhs), false);
      return {result, flags, 0};
    }

    case Operation::Push: {
      cpu.push16(input.rhs);
      return {0, input.flags, 7};
    }

    case Operation::Shift: {
      if (const auto [direction, type, fast] = std::get<ShiftArgs>(args); fast) {
        if (type == ShiftArgs::Type::RotateCircular) {
          const auto [result, flags] =
              Alu::fast_rotate_circular8(static_cast<std::uint8_t>(input.lhs), direction, input.flags);
          return {result, flags, 0};
        }
        if (type == ShiftArgs::Type::Rotate) {
          const auto [result, flags] =
              Alu::fast_rotate_circular8(static_cast<std::uint8_t>(input.lhs), direction, input.flags);
          return {result, flags, 0};
        }
      }
      else {
        switch (type) {
          case ShiftArgs::Type::RotateCircular: {
            const auto [result, flags] = Alu::rotate_circular8(static_cast<std::uint8_t>(input.lhs), direction);
            return {result, flags, 0};
          }
          case ShiftArgs::Type::Rotate: {
            const auto [result, flags] =
                Alu::rotate8(static_cast<std::uint8_t>(input.lhs), direction, input.flags.carry());
            return {result, flags, 0};
          }
          case ShiftArgs::Type::ShiftArithmetic: {
            const auto [result, flags] = Alu::shift_arithmetic8(static_cast<std::uint8_t>(input.lhs), direction);
            return {result, flags, 0};
          }
          case ShiftArgs::Type::Shift: {
            const auto [result, flags] = Alu::shift_logical8(static_cast<std::uint8_t>(input.lhs), direction);
            return {result, flags, 0};
          }
        }
      }
      break;
    }

    case Operation::Invalid: break;
  }
  // TODO better
  throw std::runtime_error("Unsupported opcode");
}

} // namespace specbolt
