#ifndef SPECBOLT_MODULES
#include <iostream>
#include <stdexcept>
#include "z80/v1/Decoder.hpp"
#endif

#include "z80/v1/Z80.hpp"


namespace specbolt::v1 {

std::size_t Z80::execute_one() {
  if (halted_) {
    ++now_tstates_;
    return 1;
  }

  regs_.r(regs_.r() + 1);
  const auto initial_time = now_tstates_;
  const auto initial_pc = regs_.pc();
  const std::array opcodes{read8(initial_pc), read8(initial_pc + 1), read8(initial_pc + 2), read8(initial_pc + 3)};
  const auto decoded = impl::decode(opcodes);
  pass_time(4);
  regs_.pc(initial_pc + decoded.length); // NOT RIGHT
  try {
    execute(decoded);
  }
  catch (...) {
    // TODO heinous
    regs_.pc(initial_pc);
    throw;
  }
  return now_tstates_ - initial_time;
}

void Z80::branch(const std::int8_t offset) { regs_.pc(static_cast<std::uint16_t>(regs_.pc() + offset)); }

std::uint16_t Z80::read(const Instruction::Operand operand, const std::int8_t index_offset) {
  switch (operand) {
    case Instruction::Operand::A: return regs_.get(RegisterFile::R8::A);
    case Instruction::Operand::B: return regs_.get(RegisterFile::R8::B);
    case Instruction::Operand::C: return regs_.get(RegisterFile::R8::C);
    case Instruction::Operand::D: return regs_.get(RegisterFile::R8::D);
    case Instruction::Operand::E: return regs_.get(RegisterFile::R8::E);
    case Instruction::Operand::H: return regs_.get(RegisterFile::R8::H);
    case Instruction::Operand::L: return regs_.get(RegisterFile::R8::L);
    case Instruction::Operand::AF: return regs_.get(RegisterFile::R16::AF);
    case Instruction::Operand::AF_: return regs_.get(RegisterFile::R16::AF_);
    case Instruction::Operand::BC: return regs_.get(RegisterFile::R16::BC);
    case Instruction::Operand::DE: return regs_.get(RegisterFile::R16::DE);
    case Instruction::Operand::HL: return regs_.get(RegisterFile::R16::HL);
    case Instruction::Operand::SP: return regs_.get(RegisterFile::R16::SP);
    case Instruction::Operand::IX: return regs_.get(RegisterFile::R16::IX);
    case Instruction::Operand::IY: return regs_.get(RegisterFile::R16::IY);
    case Instruction::Operand::IXH: return regs_.get(RegisterFile::R8::IXH);
    case Instruction::Operand::IYH: return regs_.get(RegisterFile::R8::IYH);
    case Instruction::Operand::IXL: return regs_.get(RegisterFile::R8::IXL);
    case Instruction::Operand::IYL: return regs_.get(RegisterFile::R8::IYL);
    case Instruction::Operand::I: return regs_.i();
    case Instruction::Operand::R: return regs_.r();
    case Instruction::Operand::BC_Indirect8: return read8(regs_.get(RegisterFile::R16::BC));
    case Instruction::Operand::DE_Indirect8: return read8(regs_.get(RegisterFile::R16::DE));
    case Instruction::Operand::HL_Indirect8: {
      // TODO work out when the wz is actually updated.
      const auto address = regs_.get(RegisterFile::R16::HL);
      regs_.wz(address);
      return read8(address);
    }
    case Instruction::Operand::HL_Indirect16: return read16(regs_.get(RegisterFile::R16::HL));
    case Instruction::Operand::SP_Indirect16: return read16(regs_.get(RegisterFile::R16::HL));
    case Instruction::Operand::Const_0:
    case Instruction::Operand::None: return 0;
    case Instruction::Operand::Const_1: return 1;
    case Instruction::Operand::Const_2: return 2;
    case Instruction::Operand::Const_3: return 3;
    case Instruction::Operand::Const_4: return 4;
    case Instruction::Operand::Const_5: return 5;
    case Instruction::Operand::Const_6: return 6;
    case Instruction::Operand::Const_7: return 7;
    case Instruction::Operand::Const_8: return 8;
    case Instruction::Operand::Const_16: return 16;
    case Instruction::Operand::Const_24: return 24;
    case Instruction::Operand::Const_32: return 32;
    case Instruction::Operand::Const_40: return 40;
    case Instruction::Operand::Const_48: return 48;
    case Instruction::Operand::Const_52: return 52;
    case Instruction::Operand::Const_ffff: return 0xffff;
    case Instruction::Operand::ByteImmediate: return read8(regs_.pc() - 1);
    case Instruction::Operand::ByteImmediate_A:
      return static_cast<std::uint16_t>(read8(regs_.pc() - 1) | regs_.get(RegisterFile::R8::A) << 8);
    case Instruction::Operand::WordImmediate: return read16(regs_.pc() - 2);
    case Instruction::Operand::WordImmediateIndirect8: return read8(read16(regs_.pc() - 2));
    case Instruction::Operand::WordImmediateIndirect16: return read16(read16(regs_.pc() - 2));
    case Instruction::Operand::PcOffset:
      return static_cast<std::uint16_t>(regs_.pc() + static_cast<std::int8_t>(read8(regs_.pc() - 1)));
    case Instruction::Operand::IX_Offset_Indirect8: {
      const auto address = static_cast<std::uint16_t>(regs_.ix() + index_offset);
      regs_.wz(address);
      return read8(address);
    }
    case Instruction::Operand::IY_Offset_Indirect8: {
      const auto address = static_cast<std::uint16_t>(regs_.iy() + index_offset);
      regs_.wz(address);
      return read8(address);
    }
    default: break; // TODO NOT THIS
  }
  throw std::runtime_error("bad operand for read");
}

void Z80::execute(const Instruction &instr) {
  const auto [value, flags, extra_t_states] = instr.apply(
      {read(instr.lhs, instr.index_offset), read(instr.rhs, instr.index_offset), Flags(regs_.get(RegisterFile::R8::F))},
      *this);
  regs_.set(RegisterFile::R8::F, flags.to_u8());
  pass_time(extra_t_states + instr.decode_t_states);
  write(instr.lhs, instr.index_offset, value);
}

void Z80::write(const Instruction::Operand operand, const std::int8_t index_offset, const std::uint16_t value) {
  switch (operand) {
    case Instruction::Operand::A: regs_.set(RegisterFile::R8::A, static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::B: regs_.set(RegisterFile::R8::B, static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::C: regs_.set(RegisterFile::R8::C, static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::D: regs_.set(RegisterFile::R8::D, static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::E: regs_.set(RegisterFile::R8::E, static_cast<std::uint8_t>(value)); break;
    // case Instruction::Operand::F:
    //   regs_.set(RegisterFile::R8::F, static_cast<std::uint8_t>(value));
    //   break;
    case Instruction::Operand::H: regs_.set(RegisterFile::R8::H, static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::L: regs_.set(RegisterFile::R8::L, static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::AF: regs_.set(RegisterFile::R16::AF, value); break;
    case Instruction::Operand::BC: regs_.set(RegisterFile::R16::BC, value); break;
    case Instruction::Operand::DE: regs_.set(RegisterFile::R16::DE, value); break;
    case Instruction::Operand::HL: regs_.set(RegisterFile::R16::HL, value); break;
    case Instruction::Operand::IX: regs_.set(RegisterFile::R16::IX, value); break;
    case Instruction::Operand::IY: regs_.set(RegisterFile::R16::IY, value); break;
    case Instruction::Operand::IXH: regs_.set(RegisterFile::R8::IXH, static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::IYH: regs_.set(RegisterFile::R8::IYH, static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::IXL: regs_.set(RegisterFile::R8::IXL, static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::IYL: regs_.set(RegisterFile::R8::IYL, static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::SP: regs_.sp(value); break;
    case Instruction::Operand::BC_Indirect8:
      write8(regs_.get(RegisterFile::R16::BC), static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::DE_Indirect8:
      write8(regs_.get(RegisterFile::R16::DE), static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::HL_Indirect8:
      write8(regs_.get(RegisterFile::R16::HL), static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::HL_Indirect16: write16(regs_.get(RegisterFile::R16::HL), value); break;
    case Instruction::Operand::SP_Indirect16: write16(regs_.sp(), value); break;
    case Instruction::Operand::I: regs_.i(static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::R: regs_.r(static_cast<std::uint8_t>(value)); break;
    case Instruction::Operand::None: break;
    case Instruction::Operand::ByteImmediate_A:
      // Does nothing; this is used in OUT (nn), a instruction.
      break;
    case Instruction::Operand::WordImmediate: throw std::runtime_error("Probably didn't mean to do this");
    case Instruction::Operand::WordImmediateIndirect8:
      write8(read16(regs_.pc() - 2), static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::WordImmediateIndirect16: write16(read16(regs_.pc() - 2), value); break;
    case Instruction::Operand::IX_Offset_Indirect8:
      write8(static_cast<std::uint16_t>(regs_.ix() + index_offset), static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::IY_Offset_Indirect8:
      write8(static_cast<std::uint16_t>(regs_.iy() + index_offset), static_cast<std::uint8_t>(value));
      break;
    default:
      // TODO NOT THIS
      throw std::runtime_error("bad operand for write");
  }
}


void Z80::push16(const std::uint16_t value) {
  push8(static_cast<std::uint8_t>(value >> 8));
  push8(static_cast<std::uint8_t>(value));
}

std::uint16_t Z80::pop16() {
  const auto low = pop8();
  const auto high = pop8();
  return static_cast<std::uint16_t>(high << 8) | low;
}

void Z80::push8(const std::uint8_t value) {
  // todo maybe account for time here instead of in the instructions?
  const auto new_sp = static_cast<std::uint16_t>(regs_.sp() - 1);
  write8(new_sp, value);
  regs_.sp(new_sp);
}

std::uint8_t Z80::pop8() {
  // todo maybe account for time here instead of in the instructions?
  const auto old_sp = regs_.sp();
  regs_.sp(static_cast<std::uint16_t>(old_sp + 1));
  return read8(old_sp);
}

void Z80::interrupt() {
  if (!iff1_)
    return;
  // Some dark business with parity flag here ignored.
  if (halted_) {
    halted_ = false;
    regs().pc(regs().pc() + 1);
  }
  iff1_ = iff2_ = false;
  pass_time(7);
  push16(regs().pc());
  switch (irq_mode_) {
    case 0:
    case 1: regs().pc(0x38); break;
    case 2: {
      // Assume the bus is at 0xff.
      const auto addr = static_cast<std::uint16_t>(0xff | (regs().i() << 8));
      regs().pc(read16(addr));
      break;
    }
    default: throw std::runtime_error("Inconceivable");
  }
}

void Z80::retn() {
  iff1_ = iff2_;
  regs().pc(pop16());
}

std::uint8_t Z80::read8(const std::uint16_t address) const { return memory_.read(address); }
std::uint16_t Z80::read16(const std::uint16_t address) const { return memory_.read16(address); }
void Z80::write8(const std::uint16_t address, const std::uint8_t value) { memory_.write(address, value); }
void Z80::write16(const std::uint16_t address, const std::uint16_t value) { memory_.write16(address, value); }

} // namespace specbolt::v1
