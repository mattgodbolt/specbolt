#include "Z80.hpp"

#include "Opcodes.hpp"

#include <format>
#include <iostream>
#include <stdexcept>

namespace specbolt {

// TODO flags registers and lookups therein.
// TODO consider making instructions out of composable objects? or templated functions? arg or is that too close to
//   the C++26 thing I want to demo?
void Z80::execute_one() {
  const auto initial_pc = regs_.pc();
  const auto decoded = decode(memory_.read(initial_pc), memory_.read(initial_pc + 1), memory_.read(initial_pc + 2));
  pass_time(4);
  regs_.pc(initial_pc + decoded.length); // NOT RIGHT
  execute(decoded);
}

std::uint8_t Z80::read_and_inc_pc() {
  const auto pc = regs_.pc();
  regs_.pc(pc + 1);
  return memory_.read(pc);
}

std::uint16_t Z80::read16_and_inc_pc() {
  const auto low = read_and_inc_pc();
  const auto high = read_and_inc_pc();
  return static_cast<std::uint16_t>(high << 8) | low;
}
std::uint16_t Z80::read16(const std::uint16_t address) const {
  return static_cast<uint16_t>(memory_.read(address + 1) << 8) | memory_.read(address);
}

void Z80::pass_time(const size_t tstates) { now_tstates_ += tstates; }

std::uint16_t Z80::read(const Instruction::Operand operand) const {
  switch (operand) {
    case Instruction::Operand::A:
      return regs_.get(RegisterFile::R8::A);
    case Instruction::Operand::B:
      return regs_.get(RegisterFile::R8::B);
    case Instruction::Operand::C:
      return regs_.get(RegisterFile::R8::C);
    case Instruction::Operand::D:
      return regs_.get(RegisterFile::R8::D);
    case Instruction::Operand::E:
      return regs_.get(RegisterFile::R8::E);
    case Instruction::Operand::H:
      return regs_.get(RegisterFile::R8::H);
    case Instruction::Operand::L:
      return regs_.get(RegisterFile::R8::L);
    case Instruction::Operand::AF:
      return regs_.get(RegisterFile::R16::AF);
    case Instruction::Operand::BC:
      return regs_.get(RegisterFile::R16::BC);
    case Instruction::Operand::DE:
      return regs_.get(RegisterFile::R16::DE);
    case Instruction::Operand::HL:
      return regs_.get(RegisterFile::R16::HL);
    case Instruction::Operand::I:
      return regs_.i();
    case Instruction::Operand::BC_Indirect:
      return read16(regs_.get(RegisterFile::R16::BC));
    case Instruction::Operand::DE_Indirect:
      return read16(regs_.get(RegisterFile::R16::DE));
    case Instruction::Operand::HL_Indirect:
      return read16(regs_.get(RegisterFile::R16::HL));
    case Instruction::Operand::Const_0:
    case Instruction::Operand::None:
      return 0;
    case Instruction::Operand::Const_1:
      return 1;
    case Instruction::Operand::Const_2:
      return 2;
    case Instruction::Operand::Const_4:
      return 4;
    case Instruction::Operand::Const_8:
      return 8;
    case Instruction::Operand::Const_16:
      return 16;
    case Instruction::Operand::Const_32:
      return 32;
    case Instruction::Operand::Const_64:
      return 64;
    case Instruction::Operand::Const_128:
      return 128;
    case Instruction::Operand::ByteImmediate:
      return memory_.read(regs_.pc() - 1);
    case Instruction::Operand::WordImmediate:
      return read16(regs_.pc() - 2);
    case Instruction::Operand::PcOffset:
      return static_cast<std::uint16_t>(regs_.pc() + static_cast<std::int8_t>(memory_.read(regs_.pc() - 1)));
    default:
      break; // TODO NOT THIS
  }
  throw std::runtime_error("bad operand");
}

void Z80::execute(const Instruction &instr) {
  const auto flags = Flags(regs_.get(RegisterFile::R8::F));
  switch (instr.condition) {
    case Instruction::Condition::None:
      break;
    // TODO time taken? :|
    // TODO should we pass the condition?
    case Instruction::Condition::Zero:
      if (!flags.zero())
        return;
      break;
    case Instruction::Condition::NonZero:
      if (flags.zero())
        return;
      break;
  }
  const auto source = read(instr.source);
  const auto dest_before = read(instr.dest);
  const auto result =
      Instruction::apply(instr.operation, {dest_before, source, regs_.pc(), regs_.sp(), flags, iff1_, iff2_});
  regs_.pc(result.pc);
  regs_.sp(result.sp);
  regs_.set(RegisterFile::R8::F, result.flags.to_u8());
  iff1_ = result.iff1;
  iff2_ = result.iff2;
  pass_time(result.extra_t_states);
  write(instr.dest, result.value);
}

void Z80::write(const Instruction::Operand operand, const std::uint16_t value) {
  switch (operand) {
    case Instruction::Operand::A:
      regs_.set(RegisterFile::R8::A, static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::B:
      regs_.set(RegisterFile::R8::B, static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::C:
      regs_.set(RegisterFile::R8::C, static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::D:
      regs_.set(RegisterFile::R8::D, static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::E:
      regs_.set(RegisterFile::R8::E, static_cast<std::uint8_t>(value));
      break;
    // case Instruction::Operand::F:
    //   regs_.set(RegisterFile::R8::F, static_cast<std::uint8_t>(value));
    //   break;
    case Instruction::Operand::H:
      regs_.set(RegisterFile::R8::H, static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::L:
      regs_.set(RegisterFile::R8::L, static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::AF:
      regs_.set(RegisterFile::R16::AF, value);
      break;
    case Instruction::Operand::BC:
      regs_.set(RegisterFile::R16::BC, value);
      break;
    case Instruction::Operand::DE:
      regs_.set(RegisterFile::R16::DE, value);
      break;
    case Instruction::Operand::HL:
      regs_.set(RegisterFile::R16::HL, value);
      break;
    case Instruction::Operand::BC_Indirect:
      // TODO are these all 8-bit?
      write8(regs_.get(RegisterFile::R16::BC), static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::DE_Indirect:
      write8(regs_.get(RegisterFile::R16::DE), static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::HL_Indirect:
      write8(regs_.get(RegisterFile::R16::HL), static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::I:
      regs_.i(static_cast<std::uint8_t>(value));
      break;
    case Instruction::Operand::None:
      break;
    case Instruction::Operand::ByteImmediate: {
      // Does nothing; this is (currently) used in OUT (nn), a instruction.
      // perhaps the operand should be ByteImmediateOut? But then how about IN?
      break;
    }
    default:
      // TODO NOT THIS
      throw std::runtime_error("bad operand");
  }
}

void Z80::write8(const std::uint16_t address, const std::uint8_t value) { memory_.write(address, value); }

void Z80::dump() const {
  std::print(std::cout, "Z80 dump:\n");
  std::print(std::cout, "PC: {:04x}\n", regs_.pc());
  std::print(std::cout, "SP: {:04x}\n", regs_.sp());
  std::print(std::cout, "AF: {:04x} - {}\n", regs_.get(RegisterFile::R16::AF),
      Flags(regs_.get(RegisterFile::R8::F)).to_string());
  std::print(std::cout, "BC: {:04x}\n", regs_.get(RegisterFile::R16::BC));
  std::print(std::cout, "DE: {:04x}\n", regs_.get(RegisterFile::R16::DE));
  std::print(std::cout, "HL: {:04x}\n", regs_.get(RegisterFile::R16::HL));
}

} // namespace specbolt
