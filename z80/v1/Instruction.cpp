#ifndef SPECBOLT_MODULES
#include "z80/v1/Instruction.hpp"
#include <stdexcept>
#include "z80/common/Alu.hpp"
#include "z80/v1/Z80.hpp"
#endif

namespace specbolt::v1 {

namespace {

std::uint8_t access_time(const Instruction::Operand rhs) {
  switch (rhs) {
    case Instruction::Operand::AF:
    case Instruction::Operand::BC:
    case Instruction::Operand::DE:
    case Instruction::Operand::HL:
    case Instruction::Operand::SP:
    case Instruction::Operand::AF_: return 6;
    case Instruction::Operand::ByteImmediate:
    case Instruction::Operand::ByteImmediate_A:
    case Instruction::Operand::BC_Indirect8:
    case Instruction::Operand::DE_Indirect8:
    case Instruction::Operand::HL_Indirect8: return 3;
    case Instruction::Operand::IX_Offset_Indirect8:
    case Instruction::Operand::IY_Offset_Indirect8: return 8;
    case Instruction::Operand::WordImmediateIndirect8: return 9;
    // Doesn't make sense but I am just trying to fix up the bad timings before I go all principled on this...
    case Instruction::Operand::WordImmediateIndirect16: return 6;
    case Instruction::Operand::R:
    case Instruction::Operand::I: return 1;
    default: return 0;
  }
}
std::uint8_t rmw_access_time(const Instruction::Operand lhs) {
  switch (lhs) {
    case Instruction::Operand::BC_Indirect8:
    case Instruction::Operand::DE_Indirect8:
    case Instruction::Operand::HL_Indirect8: return 7;
    default: return 0;
  }
}

std::uint8_t access_time(const Instruction::Operand lhs, const Instruction::Operand rhs) {
  // Heinous hack to make this flawed approach agree with reality.
  if (lhs == Instruction::Operand::SP && rhs == Instruction::Operand::HL)
    return 2;
  return access_time(lhs) + access_time(rhs);
}

Instruction::Output alu8(
    const Instruction::Input input, Alu::R8 (*operation)(std::uint8_t, std::uint8_t), const Instruction::Operand op) {
  const auto [result, flags] = operation(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs));
  return {result, flags, access_time(op)};
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
    case Operation::Halt: cpu.halt(); return {0, input.flags, 0};

    case Operation::Add8: {
      const auto [result, flags] =
          Alu::add8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs), carry);
      return {result, flags, access_time(lhs, rhs)};
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
      return {input.lhs, Alu::cmp8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs)).flags,
          access_time(rhs)};
    case Operation::Subtract8: {
      const auto [result, flags] =
          Alu::sub8(static_cast<std::uint8_t>(input.lhs), static_cast<std::uint8_t>(input.rhs), carry);
      return {result, flags, access_time(lhs, rhs)};
    }
    case Operation::Subtract16: {
      const auto [result, flags] = std::holds_alternative<WithCarry>(args)
                                       ? Alu::sbc16(input.lhs, input.rhs, carry)
                                       : Alu::sub16(input.lhs, input.rhs, input.flags);
      return {result, flags, 7};
    }

    case Operation::Inc8: {
      const auto [result, flags] = Alu::inc8(static_cast<std::uint8_t>(input.lhs), input.flags);
      return {result, flags, rmw_access_time(lhs)};
    }
    case Operation::Dec8: {
      const auto [result, flags] = Alu::dec8(static_cast<std::uint8_t>(input.lhs), input.flags);
      return {result, flags, rmw_access_time(lhs)};
    }

    case Operation::Load: return {input.rhs, input.flags, access_time(lhs, rhs)};
    case Operation::LoadSpecial:
      return {input.rhs, Alu::iff2_flags_for(static_cast<std::uint8_t>(input.rhs), input.flags, cpu.iff2()),
          access_time(lhs, rhs)};
    case Operation::JumpRelative:
    case Operation::Jump: {
      const auto taken = should_execute(input.flags);
      if (taken)
        cpu.registers().pc(input.rhs);
      if (rhs == Operand::HL || rhs == Operand::IX || rhs == Operand::IY)
        return {0, input.flags, 0};
      return {0, input.flags, static_cast<std::uint8_t>(operation == Operation::Jump ? 6 : taken ? 8 : 3)};
    }
    case Operation::Call: {
      const auto taken = should_execute(input.flags);
      if (taken) {
        cpu.push16(cpu.registers().pc());
        cpu.registers().pc(input.rhs);
      }
      // horrid check for rst
      if (rhs >= Operand::Const_0 && rhs <= Operand::Const_52)
        return {0, input.flags, 7};

      return {0, input.flags, static_cast<std::uint8_t>(taken ? 13 : 6)};
    }
    case Operation::Return: {
      const auto taken = should_execute(input.flags);
      const auto was_conditional = std::get_if<Condition>(&args);
      if (taken) {
        if (const auto was_retirq = std::get_if<WithIrq>(&args); was_retirq && *was_retirq == WithIrq::Retn)
          cpu.retn();
        else // this treats reti and retn the same...
          cpu.registers().pc(cpu.pop16());
      }

      return {0, input.flags, static_cast<std::uint8_t>(was_conditional ? (taken ? 7 : 1) : 6)};
    }
    case Operation::Xor: return alu8(input, &Alu::xor8, rhs);
    case Operation::And: return alu8(input, &Alu::and8, rhs);
    case Operation::Or: return alu8(input, &Alu::or8, rhs);
    case Operation::Irq:
      cpu.iff1(input.rhs);
      cpu.iff2(input.rhs);
      return {0, input.flags, 0};
    case Operation::IrqMode: cpu.irq_mode(static_cast<std::uint8_t>(input.rhs)); return {0, input.flags, 0};
    case Operation::Out:
      cpu.out(input.lhs, static_cast<std::uint8_t>(input.rhs));
      return {0, input.flags, static_cast<std::uint8_t>(4 + (lhs == Operand::ByteImmediate_A ? 3 : 0))};
    case Operation::In: {
      const auto result = cpu.in(input.rhs);
      const auto flags = std::holds_alternative<NoFlags>(args)
                             ? input.flags
                             : input.flags & Flags::Carry() | Alu::parity_flags_for(result);
      return {result, flags, static_cast<std::uint8_t>(4 + (rhs == Operand::ByteImmediate_A ? 3 : 0))};
    }
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
          const auto rhs_val = cpu.read(rhs, index_offset);
          cpu.write16(cpu.registers().sp(), rhs_val);
          cpu.write(rhs, index_offset, sp16);
          return {rhs_val, input.flags, 15}; // TODO is this really the right thing to return?
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
          const auto subtract_result = Alu::sub8(cpu.registers().get(RegisterFile::R8::A), byte, false);
          // bits 3 and 5 come from the result, where bit 3 goes to flag 5, and bit 1 to flag 3....and where if HF is
          // set we use res--....
          const auto flag_bits =
              subtract_result.flags.half_carry() ? subtract_result.result - 1 : subtract_result.result;
          flags = flags & ~(Flags::Flag3() | Flags::Flag5()) | //
                  (flag_bits & 0x08 ? Flags::Flag3() : Flags()) | //
                  (flag_bits & 0x02 ? Flags::Flag5() : Flags());
          constexpr auto flags_to_copy = Flags::HalfCarry() | Flags::Zero() | Flags::Sign() | Flags::Subtract();
          flags = flags & ~flags_to_copy | subtract_result.flags & flags_to_copy;
          if (flags.zero())
            should_continue = false;
          break;
        }
        case EdOpArgs::Op::In: throw std::runtime_error("in not supported yet TODO");
        case EdOpArgs::Op::Out: throw std::runtime_error("out not supported yet TODO");
      }
      if (should_continue) {
        cpu.registers().pc(cpu.registers().pc() - 2);
        return {0, flags, 13};
      }
      return {0, flags, 8};
    }
    case Operation::Set:
      return {static_cast<std::uint16_t>(input.lhs | static_cast<std::uint16_t>(1u << input.rhs)), input.flags,
          static_cast<std::uint8_t>(4 + rmw_access_time(lhs))};
    case Operation::Reset:
      return {static_cast<std::uint16_t>(input.lhs & ~static_cast<std::uint16_t>(1u << input.rhs)), input.flags,
          static_cast<std::uint8_t>(4 + rmw_access_time(lhs))};

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
      const auto is_indirect =
          lhs == Operand::HL_Indirect8 || lhs == Operand::IX_Offset_Indirect8 || lhs == Operand::IY_Offset_Indirect8;
      // For indirect reads, the undefined bits are the top 8 bits of the fetched address (aka "wz" register). Else
      // it's the lhs value.
      const auto bits_left_on_bus = static_cast<std::uint8_t>(is_indirect ? cpu.registers().wz() >> 8 : input.lhs);
      const auto flags = Alu::bit(static_cast<std::uint8_t>(input.lhs), bit, input.flags, bits_left_on_bus);
      return {input.lhs, flags, static_cast<std::uint8_t>(is_indirect ? 8 : 4)};
    }

    case Operation::Pop: return {cpu.pop16(), input.flags, 6};
    case Operation::Ccf: {
      const auto [result, flags] = Alu::ccf(static_cast<std::uint8_t>(input.lhs), input.flags);
      return {result, flags, 0};
    }
    case Operation::Scf: {
      const auto [result, flags] = Alu::scf(static_cast<std::uint8_t>(input.lhs), input.flags);
      return {result, flags, 0};
    }
    case Operation::Daa: {
      const auto [result, flags] = Alu::daa(static_cast<std::uint8_t>(input.lhs), input.flags);
      return {result, flags, 0};
    }
    case Operation::Cpl: {
      const auto [result, flags] = Alu::cpl(static_cast<std::uint8_t>(input.lhs), input.flags);
      return {result, flags, 0};
    }
    case Operation::Neg: {
      const auto [result, flags] = Alu::sub8(0, static_cast<std::uint8_t>(input.lhs), false);
      return {result, flags, 0};
    }

    case Operation::Push: {
      cpu.push16(input.rhs);
      return {0, input.flags, 7};
    }

    case Operation::Rrd: {
      const auto prev_a = cpu.registers().get(RegisterFile::R8::A);
      const auto new_a = static_cast<std::uint8_t>((prev_a & 0xf0) | (input.lhs & 0xf));
      cpu.registers().set(RegisterFile::R8::A, new_a);
      return {static_cast<std::uint16_t>(input.lhs >> 4 | ((prev_a & 0xf) << 4)),
          input.flags & Flags::Carry() | Alu::parity_flags_for(new_a), 10};
    }

    case Operation::Rld: {
      const auto prev_a = cpu.registers().get(RegisterFile::R8::A);
      const auto new_a = static_cast<std::uint8_t>((prev_a & 0xf0) | ((input.lhs >> 4) & 0xf));
      cpu.registers().set(RegisterFile::R8::A, new_a);
      return {static_cast<std::uint16_t>(input.lhs << 4 | (prev_a & 0xf)),
          input.flags & Flags::Carry() | Alu::parity_flags_for(new_a), 10};
    }

    case Operation::Shift: {
      if (const auto [direction, type, fast] = std::get<ShiftArgs>(args); fast) {
        if (type == ShiftArgs::Type::RotateCircular) {
          const auto [result, flags] =
              Alu::fast_rotate_circular8(static_cast<std::uint8_t>(input.lhs), direction, input.flags);
          return {result, flags, 0};
        }
        if (type == ShiftArgs::Type::Rotate) {
          const auto [result, flags] = Alu::fast_rotate8(static_cast<std::uint8_t>(input.lhs), direction, input.flags);
          return {result, flags, 0};
        }
      }
      else {
        switch (type) {
          case ShiftArgs::Type::RotateCircular: {
            const auto [result, flags] = Alu::rotate_circular8(static_cast<std::uint8_t>(input.lhs), direction);
            return {result, flags, static_cast<std::uint8_t>(4 + rmw_access_time(lhs))};
          }
          case ShiftArgs::Type::Rotate: {
            const auto [result, flags] =
                Alu::rotate8(static_cast<std::uint8_t>(input.lhs), direction, input.flags.carry());
            return {result, flags, static_cast<std::uint8_t>(4 + rmw_access_time(lhs))};
          }
          case ShiftArgs::Type::ShiftArithmetic: {
            const auto [result, flags] = Alu::shift_arithmetic8(static_cast<std::uint8_t>(input.lhs), direction);
            return {result, flags, static_cast<std::uint8_t>(4 + rmw_access_time(lhs))};
          }
          case ShiftArgs::Type::Shift: {
            const auto [result, flags] = Alu::shift_logical8(static_cast<std::uint8_t>(input.lhs), direction);
            return {result, flags, static_cast<std::uint8_t>(4 + rmw_access_time(lhs))};
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

} // namespace specbolt::v1
