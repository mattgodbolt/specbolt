#include "Z80.hpp"

#include "Opcodes.hpp"

#include <stdexcept>

namespace specbolt {

// TODO flags registers and lookups therein.
// TODO consider making instructions out of composable objects? or templated functions? arg or is that too close to
//   the C++26 thing I want to demo?
void Z80::execute_one() {
  const auto opcode = read_and_inc_pc();
  pass_time(4);

  switch (opcode) {
    case 0x00: // NOP
      break;
    case 0x01: { // LD BC, nnnn
      execute(ld_bc_nnnn);
      regs_.set(RegisterFile::R16::BC, read16_and_inc_pc());
      pass_time(6);
      break;
    }
    case 0x11: { // LD DE, nnnn
      regs_.set(RegisterFile::R16::DE, read16_and_inc_pc());
      pass_time(6);
      break;
    }
    case 0xaf: { // XOR A, A
      regs_.set(RegisterFile::R8::A, 0);
      regs_.set(RegisterFile::R8::F, 0x80); // TODO look up in table[0];? not this anyway
      break;
    }
    case 0xf3: { // DI
      iff1_ = false;
      iff2_ = false;
      break;
    }
    default:
      throw std::runtime_error("bad opcode");
  }
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

void Z80::pass_time(size_t tstates) { now_tstates_ += tstates; }

std::uint16_t Z80::read(Instruction::Operand operand) const {
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
    case Instruction::Operand::BC_Indirect:
      return read16(regs_.get(RegisterFile::R16::BC));
    case Instruction::Operand::DE_Indirect:
      return read16(regs_.get(RegisterFile::R16::DE));
    case Instruction::Operand::HL_Indirect:
      return read16(regs_.get(RegisterFile::R16::HL));
    case Instruction::Operand::One:
      return 1;
    default:
      break; // TODO NOT THIS
  }
  throw std::runtime_error("bad operand");
}


void Z80::execute(const Instruction &instr) {
  const auto source = read(instr.source);
  const auto dest_before = read(instr.dest);
  const auto dest = apply(instr.operation, dest_before, source);
  write(instr.dest, dest);
}

// Possible inputs:
// all registers, memory, flags, immediate values, offsets
// Possible outputs
// all registers, memory, flags, program counter, stack pointer.
struct InstructionInput {
  std::uint16_t dest;
  std::uint16_t source;
  std::uint16_t pc;
  std::uint16_t sp;
  std::uint8_t flags;
};
struct InstructionOutput {
  std::uint16_t value;
  std::uint16_t pc;
  std::uint16_t sp;
  std::uint8_t flags;
};
std::uint16_t Z80::apply(const Instruction::Operation operation, const std::uint16_t dest, const std::uint16_t source) {
  switch (operation) {
    case Instruction::Operation::None:
      return 0;
    case Instruction::Operation::Load:
      return source;
    case Instruction::Operation::Add:
      // TODO flags
      return dest + source;
    case Instruction::Operation::Subtract:
      // TODO flags
      return dest - source;
    default:
      throw std::runtime_error("bad operation"); // TODO NOT THIS
  }
}

void Z80::write(Instruction::Operand operand, std::uint16_t value) {
  switch (operand) {
    case Instruction::Operand::A:
      regs_.set(RegisterFile::R8::A, value);
      break;
    default:
      break; // TODO NOT THIS
  }
  throw std::runtime_error("bad operand");
}

} // namespace specbolt
