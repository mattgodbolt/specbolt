#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/common/Alu.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/v2/Mnemonic.hpp"
#include "z80/v2/Z80.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#endif

using namespace std::literals;

namespace specbolt::v2::impl {

void decode_and_run_cb(Z80 &z80);
void decode_and_run_dd(Z80 &z80);
void decode_and_run_ed(Z80 &z80);
void decode_and_run_fd(Z80 &z80);
void decode_and_run_ddcb(Z80 &z80);
void decode_and_run_fdcb(Z80 &z80);

enum class HlSet { Base, Ix, Iy };

template<HlSet hl_set>
struct IndexReg;
template<>
struct IndexReg<HlSet::Base> {
  static constexpr auto name = "hl";
  static constexpr auto name_indirect = "(hl)";
  static constexpr auto namehigh = "h";
  static constexpr auto namelow = "l";
  static constexpr auto highlow = RegisterFile::R16::HL;
  static constexpr auto high = RegisterFile::R8::H;
  static constexpr auto low = RegisterFile::R8::L;
};
template<>
struct IndexReg<HlSet::Ix> {
  static constexpr auto name = "ix";
  static constexpr auto name_indirect = "(ix$o)";
  static constexpr auto namehigh = "ixh";
  static constexpr auto namelow = "ixl";
  static constexpr auto highlow = RegisterFile::R16::IX;
  static constexpr auto high = RegisterFile::R8::IXH;
  static constexpr auto low = RegisterFile::R8::IXL;
};
template<>
struct IndexReg<HlSet::Iy> {
  static constexpr auto name = "iy";
  static constexpr auto name_indirect = "(iy$o)";
  static constexpr auto namehigh = "iyh";
  static constexpr auto namelow = "iyl";
  static constexpr auto highlow = RegisterFile::R16::IY;
  static constexpr auto high = RegisterFile::R8::IYH;
  static constexpr auto low = RegisterFile::R8::IYL;
};

template<HlSet hl_set, bool no_remap_ixiy_8b = false>
struct TableR {
  static constexpr std::array names{"b", "c", "d", "e", IndexReg<hl_set>::namehigh, IndexReg<hl_set>::namelow,
      IndexReg<hl_set>::name_indirect, "a", "$nn"};
  static constexpr std::array regs = {RegisterFile::R8::B, RegisterFile::R8::C, RegisterFile::R8::D,
      RegisterFile::R8::E, IndexReg<hl_set>::high, IndexReg<hl_set>::low, RegisterFile::R8::A /* NOT REALLY */,
      RegisterFile::R8::A};
};
template<HlSet hl_set>
struct TableR<hl_set, true> {
  static constexpr std::array names{"b", "c", "d", "e", IndexReg<HlSet::Base>::namehigh, IndexReg<HlSet::Base>::namelow,
      IndexReg<hl_set>::name_indirect, "a", "$nn"};
  static constexpr std::array regs = {RegisterFile::R8::B, RegisterFile::R8::C, RegisterFile::R8::D,
      RegisterFile::R8::E, IndexReg<HlSet::Base>::high, IndexReg<HlSet::Base>::low,
      RegisterFile::R8::A /* NOT REALLY */, RegisterFile::R8::A};
};

template<HlSet hl_set>
struct TableRp {
  static constexpr std::array names = {"bc", "de", IndexReg<hl_set>::name, "sp"};
  static constexpr std::array high = {
      RegisterFile::R8::B, RegisterFile::R8::D, IndexReg<hl_set>::high, RegisterFile::R8::SPH};
  static constexpr std::array low = {
      RegisterFile::R8::C, RegisterFile::R8::E, IndexReg<hl_set>::low, RegisterFile::R8::SPL};
  static constexpr std::array highlow = {
      RegisterFile::R16::BC, RegisterFile::R16::DE, IndexReg<hl_set>::highlow, RegisterFile::R16::SP};
};

template<HlSet hl_set>
struct TableRp2 {
  static constexpr std::array names = {"bc", "de", IndexReg<hl_set>::name, "af"};
  static constexpr std::array highlow = {
      RegisterFile::R16::BC, RegisterFile::R16::DE, IndexReg<hl_set>::highlow, RegisterFile::R16::AF};
};

constexpr auto is_r_indirect(const std::uint8_t index) { return index == 6; }

template<std::uint8_t index, HlSet hl_set, bool no_remap_ixiy_8b>
struct OperandGetSetter {
  constexpr static std::uint8_t get(Z80 &z80) { return z80.regs().get(TableR<hl_set, no_remap_ixiy_8b>::regs[index]); }
  constexpr static void set(Z80 &z80, const std::uint8_t value) {
    z80.regs().set(TableR<hl_set, no_remap_ixiy_8b>::regs[index], value);
  }
};

// Special case for indirect indexed register.
template<HlSet hl_set, bool no_remap_ixiy_8b>
struct OperandGetSetter<6, hl_set, no_remap_ixiy_8b> {
  constexpr static std::uint8_t get(Z80 &z80) { return z80.read(z80.regs().wz()); }
  constexpr static void set(Z80 &z80, const std::uint8_t value) { z80.write(z80.regs().wz(), value); }
};

#if __cpp_deleted_function
#define SPECBOLT_DELETE_REASON(text) delete (text)
#else
#define SPECBOLT_DELETE_REASON(text) delete
#endif

template<HlSet hl_set, bool no_remap_ixiy_8b>
struct OperandGetSetter<8, hl_set, no_remap_ixiy_8b> {
  constexpr static std::uint8_t get(Z80 &z80) { return z80.read_immediate(); };
  constexpr static void set(Z80 &z80, std::uint8_t) = SPECBOLT_DELETE_REASON("Cannot set immediate value");
};

template<std::uint8_t y, HlSet hl_set, bool no_remap_ixiy_8b = false>
constexpr std::uint8_t get_r(Z80 &z80) {
  return OperandGetSetter<y, hl_set, no_remap_ixiy_8b>::get(z80);
}
template<std::uint8_t y, HlSet hl_set, bool no_remap_ixiy_8b = false>
constexpr void set_r(Z80 &z80, const std::uint8_t value) {
  OperandGetSetter<y, hl_set, no_remap_ixiy_8b>::set(z80, value);
}

constexpr std::array cc_names = {"nz", "z", "nc", "c", "po", "pe", "p", "m"};
template<std::uint8_t>
inline bool cc_check(Flags flags) = SPECBOLT_DELETE_REASON("invalid condition code");
template<>
inline bool cc_check<0>(const Flags flags) {
  return !flags.zero();
}
template<>
inline bool cc_check<1>(const Flags flags) {
  return flags.zero();
}
template<>
inline bool cc_check<2>(const Flags flags) {
  return !flags.carry();
}
template<>
inline bool cc_check<3>(const Flags flags) {
  return flags.carry();
}
template<>
inline bool cc_check<4>(const Flags flags) {
  return !flags.parity();
}
template<>
inline bool cc_check<5>(const Flags flags) {
  return flags.parity();
}
template<>
inline bool cc_check<6>(const Flags flags) {
  return !flags.sign();
}
template<>
inline bool cc_check<7>(const Flags flags) {
  return flags.sign();
}

struct Nop {
  static constexpr Mnemonic mnemonic{"nop"};
  static constexpr void execute(Z80 &) {}
  static constexpr bool indirect = false;
};

template<std::uint8_t p, HlSet hl_set>
struct Load16ImmOp {
  using TableRp = impl::TableRp<hl_set>;
  static constexpr Mnemonic mnemonic{"ld "s + TableRp::names[p] + ", $nnnn"};
  static constexpr bool indirect = false;

  static constexpr auto execute(Z80 &z80) {
    // TODO: just go straight to low/high? or is there a visible difference?
    z80.regs().set(TableRp::low[p], z80.read_immediate());
    z80.regs().set(TableRp::high[p], z80.read_immediate());
  }
};

template<Mnemonic mnem, auto op, bool ind = false>
  requires(std::invocable<decltype(op), Z80 &>)
struct Op {
  static constexpr auto mnemonic = mnem;
  static constexpr auto execute = op;
  static constexpr auto indirect = ind;
};

// http://www.z80.info/decoding.htm
struct Opcode {
  std::uint8_t x;
  std::uint8_t y;
  std::uint8_t z;
  std::uint8_t p;
  std::uint8_t q;
  HlSet hl_set;

  explicit constexpr Opcode(const std::size_t opcode, const HlSet hl_set_) :
      x(static_cast<std::uint8_t>(opcode) >> 6), y((static_cast<std::uint8_t>(opcode) >> 3) & 0x7),
      z(static_cast<std::uint8_t>(opcode) & 0x7), p(y >> 1), q(y & 1), hl_set(hl_set_) {
    if (opcode >= 0x100)
      throw std::runtime_error("Bad opcode");
  }

  [[nodiscard]] constexpr bool is_alu_op() const { return x == 2 || (x == 3 && z == 6); }
  [[nodiscard]] constexpr int alu_op() const { return is_alu_op() ? y : -1; }
  [[nodiscard]] constexpr std::uint8_t alu_input_selector() const { return x == 2 ? z : 8; }
};

template<Opcode>
struct MissingInstruction {
  MissingInstruction() = SPECBOLT_DELETE_REASON("Instruction not defined");
};

struct InvalidInstruction {
  static constexpr Mnemonic mnemonic{"??"};
  static constexpr void execute(Z80 &) {}
};

template<Opcode opcode>
constexpr auto instruction = MissingInstruction<opcode>{};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X = 0
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 0 && opcode.z == 0)
constexpr auto instruction<opcode> = Nop{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 1 && opcode.z == 0)
constexpr auto instruction<opcode> =
    Op<"ex af, af'", [](Z80 &z80) { z80.regs().ex(RegisterFile::R16::AF, RegisterFile::R16::AF_); }>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 2 && opcode.z == 0)
constexpr auto instruction<opcode> = Op<"djnz $d", [](Z80 &z80) {
  z80.pass_time(1);
  const auto offset = static_cast<std::int8_t>(z80.read_immediate());
  const std::uint8_t new_b = z80.regs().get(RegisterFile::R8::B) - 1;
  z80.regs().set(RegisterFile::R8::B, new_b);
  if (new_b == 0)
    return;
  z80.pass_time(5);
  z80.branch(offset);
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 3 && opcode.z == 0)
constexpr auto instruction<opcode> = Op<"jr $d", [](Z80 &z80) {
  const auto offset = static_cast<std::int8_t>(z80.read_immediate());
  z80.pass_time(5);
  z80.branch(offset);
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && (opcode.y >= 4 && opcode.y <= 7) && opcode.z == 0)
constexpr auto instruction<opcode> = Op<"jr "s + cc_names[opcode.y - 4] + " $d", [](Z80 &z80) {
  const auto offset = static_cast<std::int8_t>(z80.read_immediate());
  if (cc_check<opcode.y - 4>(z80.flags())) {
    z80.pass_time(5);
    z80.branch(offset);
  }
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 1 && opcode.q == 0)
constexpr auto instruction<opcode> = Load16ImmOp<opcode.p, opcode.hl_set>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 1 && opcode.q == 1)
constexpr auto instruction<opcode> =
    Op<"add "s + IndexReg<opcode.hl_set>::name + ", " + TableRp<opcode.hl_set>::names[opcode.p], [](Z80 &z80) {
      const auto rhs = z80.regs().get(TableRp<opcode.hl_set>::highlow[opcode.p]);
      const auto [result, flags] = Alu::add16(z80.regs().get(IndexReg<opcode.hl_set>::highlow), rhs, z80.flags());
      z80.regs().set(IndexReg<opcode.hl_set>::highlow, result);
      z80.flags(flags);
      z80.pass_time(7);
    }>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 0)
constexpr auto instruction<opcode> = Op<"ld (bc), a",
    [](Z80 &z80) { z80.write(z80.regs().get(RegisterFile::R16::BC), z80.regs().get(RegisterFile::R8::A)); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 1)
constexpr auto instruction<opcode> = Op<"ld (de), a",
    [](Z80 &z80) { z80.write(z80.regs().get(RegisterFile::R16::DE), z80.regs().get(RegisterFile::R8::A)); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 2)
constexpr auto instruction<opcode> = Op<"ld ($nnnn), "s + IndexReg<opcode.hl_set>::name, [](Z80 &z80) {
  const auto address = z80.read_immediate16();
  z80.write(address, z80.regs().get(IndexReg<opcode.hl_set>::low));
  z80.write(address + 1, z80.regs().get(IndexReg<opcode.hl_set>::high));
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 3)
constexpr auto instruction<opcode> = Op<"ld ($nnnn), a", [](Z80 &z80) {
  const auto address = z80.read_immediate16();
  z80.write(address, z80.regs().get(RegisterFile::R8::A));
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 0)
constexpr auto instruction<opcode> = Op<"ld a, (bc)",
    [](Z80 &z80) { z80.regs().set(RegisterFile::R8::A, z80.read(z80.regs().get(RegisterFile::R16::BC))); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 1)
constexpr auto instruction<opcode> = Op<"ld a, (de)",
    [](Z80 &z80) { z80.regs().set(RegisterFile::R8::A, z80.read(z80.regs().get(RegisterFile::R16::DE))); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 2)
constexpr auto instruction<opcode> = Op<"ld "s + IndexReg<opcode.hl_set>::name + ", ($nnnn)", [](Z80 &z80) {
  const auto address = z80.read_immediate16();
  z80.regs().set(IndexReg<opcode.hl_set>::low, z80.read(address));
  z80.regs().set(IndexReg<opcode.hl_set>::high, z80.read(address + 1));
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 3)
constexpr auto instruction<opcode> = Op<"ld a, ($nnnn)", [](Z80 &z80) {
  const auto address = z80.read_immediate16();
  z80.regs().set(RegisterFile::R8::A, z80.read(address));
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 3 && opcode.q == 0)
constexpr auto instruction<opcode> = Op<"inc "s + TableRp<opcode.hl_set>::names[opcode.p], [](Z80 &z80) {
  z80.regs().set(
      TableRp<opcode.hl_set>::highlow[opcode.p], z80.regs().get(TableRp<opcode.hl_set>::highlow[opcode.p]) + 1);
  z80.pass_time(2);
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 3 && opcode.q == 1)
constexpr auto instruction<opcode> = Op<"dec "s + TableRp<opcode.hl_set>::names[opcode.p], [](Z80 &z80) {
  z80.regs().set(
      TableRp<opcode.hl_set>::highlow[opcode.p], z80.regs().get(TableRp<opcode.hl_set>::highlow[opcode.p]) - 1);
  z80.pass_time(2);
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 4)
constexpr auto instruction<opcode> = Op<"inc "s + TableR<opcode.hl_set>::names[opcode.y],
    [](Z80 &z80) {
      const auto rhs = get_r<opcode.y, opcode.hl_set>(z80);
      if constexpr (opcode.y == 6) // TODO can we better generalise?
        z80.pass_time(1);
      const auto [result, flags] = Alu::inc8(rhs, z80.flags());
      set_r<opcode.y, opcode.hl_set>(z80, result);
      z80.flags(flags);
    },
    is_r_indirect(opcode.y)>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 5)
constexpr auto instruction<opcode> = Op<"dec "s + TableR<opcode.hl_set>::names[opcode.y],
    [](Z80 &z80) {
      const auto rhs = get_r<opcode.y, opcode.hl_set>(z80);
      if constexpr (opcode.y == 6) // TODO can we better generalise?
        z80.pass_time(1);
      const auto [result, flags] = Alu::dec8(rhs, z80.flags());
      set_r<opcode.y, opcode.hl_set>(z80, result);
      z80.flags(flags);
    },
    is_r_indirect(opcode.y)>{};

template<Opcode opcode>
struct LoadImmediate8 {
  static constexpr Mnemonic mnemonic{"ld "s + TableR<opcode.hl_set>::names[opcode.y] + ", $nn"};
  static constexpr auto execute = [](Z80 &z80) { set_r<opcode.y, opcode.hl_set>(z80, z80.read_immediate()); };
  static constexpr auto indirect = is_r_indirect(opcode.y);
  static constexpr auto is_load_immediate = true;
};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 6)
constexpr auto instruction<opcode> = LoadImmediate8<opcode>{};

template<Mnemonic mnem, auto alu_op>
struct FastAluOp {
  static constexpr Mnemonic mnemonic = mnem;
  static constexpr void execute(Z80 &z80) {
    const auto [result, flags] = alu_op(z80.regs().get(RegisterFile::R8::A), z80.flags());
    z80.regs().set(RegisterFile::R8::A, result);
    z80.flags(flags);
  }
  static constexpr bool indirect = false;
};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 0)
constexpr auto instruction<opcode> = FastAluOp<"rlca", [](const std::uint8_t a, const Flags flags) {
  return Alu::fast_rotate_circular8(a, Alu::Direction::Left, flags);
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 1)
constexpr auto instruction<opcode> = FastAluOp<"rrca", [](const std::uint8_t a, const Flags flags) {
  return Alu::fast_rotate_circular8(a, Alu::Direction::Right, flags);
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 2)
constexpr auto instruction<opcode> = FastAluOp<"rla",
    [](const std::uint8_t a, const Flags flags) { return Alu::fast_rotate8(a, Alu::Direction::Left, flags); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 3)
constexpr auto instruction<opcode> = FastAluOp<"rra",
    [](const std::uint8_t a, const Flags flags) { return Alu::fast_rotate8(a, Alu::Direction::Right, flags); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 4)
constexpr auto instruction<opcode> =
    FastAluOp<"daa", [](const std::uint8_t a, const Flags flags) { return Alu::daa(a, flags); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 5)
constexpr auto instruction<opcode> =
    FastAluOp<"cpl", [](const std::uint8_t a, const Flags flags) { return Alu::cpl(a, flags); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 6)
constexpr auto instruction<opcode> =
    FastAluOp<"scf", [](const std::uint8_t a, const Flags flags) { return Alu::scf(a, flags); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 7)
constexpr auto instruction<opcode> =
    FastAluOp<"ccf", [](const std::uint8_t a, const Flags flags) { return Alu::ccf(a, flags); }>{};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X = 1
template<Opcode opcode>
  requires(opcode.x == 1 && !(opcode.y == 6 && opcode.z == 6))
constexpr auto instruction<opcode> = Op<"ld "s + TableR<opcode.hl_set, is_r_indirect(opcode.z)>::names[opcode.y] +
                                            ", " + TableR<opcode.hl_set, is_r_indirect(opcode.y)>::names[opcode.z],
    [](Z80 &z80) {
      set_r<opcode.y, opcode.hl_set, is_r_indirect(opcode.z)>(
          z80, get_r<opcode.z, opcode.hl_set, is_r_indirect(opcode.y)>(z80));
    },
    is_r_indirect(opcode.y) || is_r_indirect(opcode.z)>{};
template<Opcode opcode>
  requires(opcode.x == 1 && opcode.y == 6 && opcode.z == 6)
constexpr auto instruction<opcode> = Op<"halt", [](Z80 &z80) { z80.halt(); }>{};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X = 2 (and parts of X = 3)
template<Mnemonic mnem, Opcode opcode, auto alu_op>
struct AluOp {
  static constexpr Mnemonic mnemonic{mnem.str() + " " + TableR<opcode.hl_set>::names[opcode.alu_input_selector()]};
  static constexpr void execute(Z80 &z80) {
    const auto [result, flags] = alu_op(
        z80.regs().get(RegisterFile::R8::A), get_r<opcode.alu_input_selector(), opcode.hl_set>(z80), z80.flags());
    z80.regs().set(RegisterFile::R8::A, result);
    z80.flags(flags);
  }
  static constexpr bool indirect = is_r_indirect(opcode.alu_input_selector());
};

template<Opcode opcode>
  requires(opcode.alu_op() == 0)
constexpr auto instruction<opcode> = AluOp<"add a,", opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::add8(lhs, rhs, false); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 1)
constexpr auto instruction<opcode> =
    AluOp<"adc a,", opcode, [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags flags) {
      return Alu::add8(lhs, rhs, flags.carry());
    }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 2)
constexpr auto instruction<opcode> = AluOp<"sub a,", opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::sub8(lhs, rhs, false); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 3)
constexpr auto instruction<opcode> =
    AluOp<"sbc a,", opcode, [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags flags) {
      return Alu::sub8(lhs, rhs, flags.carry());
    }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 4)
constexpr auto instruction<opcode> = AluOp<"and", opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::and8(lhs, rhs); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 5)
constexpr auto instruction<opcode> = AluOp<"xor", opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::xor8(lhs, rhs); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 6)
constexpr auto instruction<opcode> = AluOp<"or", opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::or8(lhs, rhs); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 7)
constexpr auto instruction<opcode> = AluOp<"cp", opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::cmp8(lhs, rhs); }>{};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X = 3 (some alu ops are defined in X = 1 above)
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 0)
constexpr auto instruction<opcode> = Op<"ret "s + cc_names[opcode.y], [](Z80 &z80) {
  z80.pass_time(1);
  if (cc_check<opcode.y>(z80.flags())) {
    const auto return_address = z80.pop16();
    z80.regs().pc(return_address);
  }
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 0)
constexpr auto instruction<opcode> = Op<"pop "s + TableRp2<opcode.hl_set>::names[opcode.p], [](Z80 &z80) {
  const auto result = z80.pop16();
  z80.regs().set(TableRp2<opcode.hl_set>::highlow[opcode.p], result);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 1 && opcode.p == 0)
constexpr auto instruction<opcode> = Op<"ret", [](Z80 &z80) {
  const auto return_address = z80.pop16();
  z80.regs().pc(return_address);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 1 && opcode.p == 1)
constexpr auto instruction<opcode> = Op<"exx", [](Z80 &z80) { z80.regs().exx(); }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 1 && opcode.p == 2)
constexpr auto instruction<opcode> = Op<"jp ("s + IndexReg<opcode.hl_set>::name + ")",
    [](Z80 &z80) { z80.regs().pc(z80.regs().get(IndexReg<opcode.hl_set>::highlow)); }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 1 && opcode.p == 3)
constexpr auto instruction<opcode> = Op<"ld sp, "s + IndexReg<opcode.hl_set>::name, [](Z80 &z80) {
  z80.pass_time(2);
  z80.regs().sp(z80.regs().get(IndexReg<opcode.hl_set>::highlow));
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 2)
constexpr auto instruction<opcode> = Op<"jp "s + cc_names[opcode.y] + ", $nnnn", [](Z80 &z80) {
  const auto jump_address = z80.read_immediate16();
  if (cc_check<opcode.y>(z80.flags())) {
    z80.regs().pc(jump_address);
  }
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 0)
constexpr auto instruction<opcode> = Op<"jp $nnnn", [](Z80 &z80) {
  const auto jump_address = z80.read_immediate16();
  z80.regs().pc(jump_address);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 1)
constexpr auto instruction<opcode> = Op<"CB", [](Z80 &z80) {
  if constexpr (opcode.hl_set == HlSet::Base)
    decode_and_run_cb(z80);
  else if constexpr (opcode.hl_set == HlSet::Ix)
    decode_and_run_ddcb(z80);
  else if constexpr (opcode.hl_set == HlSet::Iy)
    decode_and_run_fdcb(z80);
  else
    static_assert(false, "this shouldn't happen");
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 2)
constexpr auto instruction<opcode> = Op<"out ($nn), a", [](Z80 &z80) {
  const auto a = z80.regs().get(RegisterFile::R8::A);
  const auto port = static_cast<std::uint16_t>(z80.read_immediate() | a << 8);
  z80.pass_time(4); // OUT time (TODO pass time as IO?)
  z80.out(port, a);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 3)
constexpr auto instruction<opcode> = Op<"in a, ($nn)", [](Z80 &z80) {
  const auto port = static_cast<std::uint16_t>(z80.read_immediate() | z80.regs().get(RegisterFile::R8::A) << 8);
  z80.pass_time(4); // IN time (TODO pass time as IO?)
  z80.regs().set(RegisterFile::R8::A, z80.in(port));
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 4)
constexpr auto instruction<opcode> = Op<"ex (sp), "s + IndexReg<opcode.hl_set>::name, [](Z80 &z80) {
  const auto sp_old_low = z80.read(z80.regs().sp());
  z80.pass_time(1);
  const auto sp_old_high = z80.read(z80.regs().sp() + 1);
  z80.write(z80.regs().sp(), z80.regs().get(IndexReg<opcode.hl_set>::low));
  z80.pass_time(2);
  z80.write(z80.regs().sp() + 1, z80.regs().get(IndexReg<opcode.hl_set>::high));
  z80.regs().set(IndexReg<opcode.hl_set>::highlow, static_cast<std::uint16_t>(sp_old_low | sp_old_high << 8));
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 5)
constexpr auto instruction<opcode> = Op<"ex de, "s + IndexReg<opcode.hl_set>::name,
    [](Z80 &z80) { z80.regs().ex(RegisterFile::R16::DE, IndexReg<opcode.hl_set>::highlow); }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 6)
constexpr auto instruction<opcode> = Op<"di", [](Z80 &z80) {
  z80.iff1(false);
  z80.iff2(false);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 7)
constexpr auto instruction<opcode> = Op<"ei", [](Z80 &z80) {
  z80.iff1(true);
  z80.iff2(true);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 4)
constexpr auto instruction<opcode> = Op<"call "s + cc_names[opcode.y] + ", $nnnn", [](Z80 &z80) {
  const auto jump_address = z80.read_immediate16();
  if (cc_check<opcode.y>(z80.flags())) {
    z80.pass_time(1);
    z80.push16(z80.pc());
    z80.regs().pc(jump_address);
  }
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 0)
constexpr auto instruction<opcode> = Op<"push "s + TableRp2<opcode.hl_set>::names[opcode.p], [](Z80 &z80) {
  z80.pass_time(1);
  z80.push16(z80.regs().get(TableRp2<opcode.hl_set>::highlow[opcode.p]));
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 1 && opcode.p == 0)
constexpr auto instruction<opcode> = Op<"call $nnnn", [](Z80 &z80) {
  const auto jump_address = z80.read_immediate16();
  z80.pass_time(1);
  z80.push16(z80.pc());
  z80.regs().pc(jump_address);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 1 && opcode.p == 1)
constexpr auto instruction<opcode> = Op<"DD", [](Z80 &z80) { decode_and_run_dd(z80); }>{};
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 1 && opcode.p == 2)
constexpr auto instruction<opcode> = Op<"ED", [](Z80 &z80) { decode_and_run_ed(z80); }>{};
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 1 && opcode.p == 3)
constexpr auto instruction<opcode> = Op<"FD", [](Z80 &z80) { decode_and_run_fd(z80); }>{};

constexpr std::array num_table{"0x00", "0x08", "0x10", "0x18", "0x20", "0x28", "0x30", "0x38"};
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 7)
constexpr auto instruction<opcode> = Op<"rst "s + num_table[opcode.y], [](Z80 &z80) {
  z80.pass_time(1);
  z80.push16(z80.pc());
  z80.regs().pc(opcode.y * 8);
}>{};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CB prefixed opcodes
template<Opcode opcode>
constexpr auto cb_instruction = MissingInstruction<opcode>{};

template<Mnemonic mnem, Opcode opcode, auto rotate_op>
struct RotateOp {
  static constexpr Mnemonic mnemonic{mnem.str() + " " + TableR<opcode.hl_set>::names[opcode.z]};
  static constexpr void execute(Z80 &z80) {
    const auto [result, flags] = rotate_op(get_r<opcode.z, opcode.hl_set>(z80), z80.flags());
    if constexpr (opcode.z == 6) // TODO can we better generalise?
      z80.pass_time(1);
    set_r<opcode.z, opcode.hl_set>(z80, result);
    z80.flags(flags);
  }
  static constexpr bool indirect = is_r_indirect(opcode.z);
};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 0)
constexpr auto cb_instruction<opcode> = RotateOp<"rlc", opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::rotate_circular8(lhs, Alu::Direction::Left); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 1)
constexpr auto cb_instruction<opcode> = RotateOp<"rrc", opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::rotate_circular8(lhs, Alu::Direction::Right); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 2)
constexpr auto cb_instruction<opcode> = RotateOp<"rl", opcode,
    [](const std::uint8_t lhs, const Flags flags) { return Alu::rotate8(lhs, Alu::Direction::Left, flags.carry()); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 3)
constexpr auto cb_instruction<opcode> = RotateOp<"rr", opcode, [](const std::uint8_t lhs, const Flags flags) {
  return Alu::rotate8(lhs, Alu::Direction::Right, flags.carry());
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 4)
constexpr auto cb_instruction<opcode> = RotateOp<"sla", opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::shift_arithmetic8(lhs, Alu::Direction::Left); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 5)
constexpr auto cb_instruction<opcode> = RotateOp<"sra", opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::shift_arithmetic8(lhs, Alu::Direction::Right); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 6)
constexpr auto cb_instruction<opcode> = RotateOp<"sll", opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::shift_logical8(lhs, Alu::Direction::Left); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 7)
constexpr auto cb_instruction<opcode> = RotateOp<"srl", opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::shift_logical8(lhs, Alu::Direction::Right); }>{};

template<Opcode opcode>
struct BitOp {
  static constexpr Mnemonic mnemonic{
      "bit "s + std::string(1, '0' + opcode.y) + ", " + TableR<opcode.hl_set>::names[opcode.z]};
  static constexpr void execute(Z80 &z80) {
    const auto lhs = get_r<opcode.z, opcode.hl_set>(z80);
    if constexpr (opcode.z == 6) // TODO can we better generalise?
      z80.pass_time(1);
    const auto bus_noise = static_cast<std::uint8_t>(opcode.z == 6 ? (z80.regs().wz() >> 8) : lhs);
    z80.flags(Alu::bit(lhs, static_cast<std::uint8_t>(1 << opcode.y), z80.flags(), bus_noise));
  }
  static constexpr auto indirect = is_r_indirect(opcode.z);
};
template<Opcode opcode>
  requires(opcode.x == 1)
constexpr auto cb_instruction<opcode> = BitOp<opcode>{};

template<Opcode opcode>
struct SetResetOp {
  static constexpr Mnemonic mnemonic{(opcode.x == 2 ? "res " : "set ") + std::string(1, '0' + opcode.y) + ", " +
                                     TableR<opcode.hl_set>::names[opcode.z]};
  static constexpr void execute(Z80 &z80) {
    const auto lhs = get_r<opcode.z, opcode.hl_set>(z80);
    if constexpr (opcode.z == 6) // TODO can we better generalise?
      z80.pass_time(1);
    const auto bit = static_cast<std::uint8_t>(1 << opcode.y);
    const auto result = static_cast<std::uint8_t>(opcode.x == 2 ? lhs & ~bit : lhs | bit);
    set_r<opcode.z, opcode.hl_set>(z80, result);
  }
  static constexpr auto indirect = is_r_indirect(opcode.z);
};
template<Opcode opcode>
  requires(opcode.x == 2 || opcode.x == 3)
constexpr auto cb_instruction<opcode> = SetResetOp<opcode>{};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ED prefixed opcodes
template<Opcode opcode>
constexpr auto ed_instruction = InvalidInstruction{};

template<Opcode opcode>
  requires(opcode.x == 0 || opcode.x == 3)
constexpr auto ed_instruction<opcode> = InvalidInstruction{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 0 && opcode.y != 6)
constexpr auto ed_instruction<opcode> = Op<"in "s + TableR<opcode.hl_set>::names[opcode.y] + ", (c)", [](Z80 &z80) {
  z80.pass_time(4); // IN time (TODO pass time as IO?)
  const auto port = z80.regs().get(RegisterFile::R16::BC);
  const auto result = z80.in(port);
  z80.flags(z80.flags() & Flags::Carry() | Alu::parity_flags_for(result));
  set_r<opcode.y, opcode.hl_set>(z80, result);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 0 && opcode.y == 6)
constexpr auto ed_instruction<opcode> = Op<"in (c)", [](Z80 &z80) {
  z80.pass_time(4); // IN time (TODO pass time as IO?)
  const auto port = z80.regs().get(RegisterFile::R16::BC);
  const auto result = z80.in(port);
  z80.flags(z80.flags() & Flags::Carry() | Alu::parity_flags_for(result));
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 1 && opcode.y != 6)
constexpr auto ed_instruction<opcode> = Op<"out (c), "s + TableR<opcode.hl_set>::names[opcode.y], [](Z80 &z80) {
  z80.pass_time(4); // IN time (TODO pass time as IO?)
  z80.out(z80.regs().get(RegisterFile::R16::BC), get_r<opcode.y, opcode.hl_set>(z80));
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 1 && opcode.y == 6)
constexpr auto ed_instruction<opcode> = Op<"out (c), 0x00", [](Z80 &z80) {
  z80.pass_time(4); // IN time (TODO pass time as IO?)
  z80.out(z80.regs().get(RegisterFile::R16::BC), 0);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 2 && opcode.q == 0)
constexpr auto ed_instruction<opcode> = Op<"sbc hl, "s + TableRp<opcode.hl_set>::names[opcode.p], [](Z80 &z80) {
  const auto rhs = z80.regs().get(TableRp<opcode.hl_set>::highlow[opcode.p]);
  const auto lhs = z80.regs().get(RegisterFile::R16::HL);
  const auto [result, flags] = Alu::sbc16(lhs, rhs, z80.flags().carry());
  z80.regs().set(RegisterFile::R16::HL, result);
  z80.flags(flags);
  z80.pass_time(7);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 2 && opcode.q == 1)
constexpr auto ed_instruction<opcode> = Op<"adc hl, "s + TableRp<opcode.hl_set>::names[opcode.p], [](Z80 &z80) {
  const auto rhs = z80.regs().get(TableRp<opcode.hl_set>::highlow[opcode.p]);
  const auto lhs = z80.regs().get(RegisterFile::R16::HL);
  const auto [result, flags] = Alu::adc16(lhs, rhs, z80.flags().carry());
  z80.regs().set(RegisterFile::R16::HL, result);
  z80.flags(flags);
  z80.pass_time(7);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 3 && opcode.q == 0)
constexpr auto ed_instruction<opcode> = Op<"ld ($nnnn), "s + TableRp<opcode.hl_set>::names[opcode.p], [](Z80 &z80) {
  const auto addr = z80.read_immediate16();
  z80.write(addr, z80.regs().get(TableRp<opcode.hl_set>::low[opcode.p]));
  z80.write(addr + 1, z80.regs().get(TableRp<opcode.hl_set>::high[opcode.p]));
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 3 && opcode.q == 1)
constexpr auto ed_instruction<opcode> =
    Op<"ld "s + TableRp<opcode.hl_set>::names[opcode.p] + ", ($nnnn)", [](Z80 &z80) {
      const auto addr = z80.read_immediate16();
      z80.regs().set(TableRp<opcode.hl_set>::low[opcode.p], z80.read(addr));
      z80.regs().set(TableRp<opcode.hl_set>::high[opcode.p], z80.read(addr + 1));
    }>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 4)
constexpr auto ed_instruction<opcode> = Op<"neg", [](Z80 &z80) {
  const auto [result, flags] = Alu::sub8(0, static_cast<std::uint8_t>(z80.regs().get(RegisterFile::R8::A)), false);
  z80.regs().set(RegisterFile::R8::A, result);
  z80.flags(flags);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 5 && opcode.y != 1)
constexpr auto ed_instruction<opcode> = Op<"retn", [](Z80 &z80) {
  z80.iff1(z80.iff2());
  const auto return_address = z80.pop16();
  z80.regs().pc(return_address);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 5 && opcode.y == 1)
constexpr auto ed_instruction<opcode> = Op<"reti", [](Z80 &z80) {
  const auto return_address = z80.pop16();
  z80.regs().pc(return_address);
}>{};

constexpr std::array<std::uint8_t, 8> im_table{0, 0, 1, 2, 0, 0, 1, 2};
template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 6)
constexpr auto ed_instruction<opcode> =
    Op<"im "s + std::string(1, '0' + im_table[opcode.y]), [](Z80 &z80) { z80.irq_mode(im_table[opcode.y]); }>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 0)
constexpr auto ed_instruction<opcode> = Op<"ld i, a", [](Z80 &z80) {
  z80.pass_time(1);
  z80.regs().i(z80.regs().get(RegisterFile::R8::A));
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 1)
constexpr auto ed_instruction<opcode> = Op<"ld r, a", [](Z80 &z80) {
  z80.pass_time(1);
  z80.regs().r(z80.regs().get(RegisterFile::R8::A));
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 2)
constexpr auto ed_instruction<opcode> = Op<"ld a, i", [](Z80 &z80) {
  const auto result = z80.regs().i();
  z80.pass_time(1);
  z80.flags(Alu::iff2_flags_for(result, z80.flags(), z80.iff2()));
  z80.regs().set(RegisterFile::R8::A, result);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 3)
constexpr auto ed_instruction<opcode> = Op<"ld a, r", [](Z80 &z80) {
  const auto result = z80.regs().r();
  z80.pass_time(1);
  z80.flags(Alu::iff2_flags_for(result, z80.flags(), z80.iff2()));
  z80.regs().set(RegisterFile::R8::A, result);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 4)
constexpr auto ed_instruction<opcode> = Op<"rrd", [](Z80 &z80) {
  const auto address = z80.regs().get(RegisterFile::R16::HL);
  const auto ind_hl = z80.read(address);
  const auto prev_a = z80.regs().get(RegisterFile::R8::A);
  const auto new_a = static_cast<std::uint8_t>((prev_a & 0xf0) | (ind_hl & 0xf));
  z80.regs().set(RegisterFile::R8::A, new_a);
  z80.pass_time(4);
  z80.write(address, static_cast<std::uint8_t>(ind_hl >> 4 | ((prev_a & 0xf) << 4)));
  z80.flags(z80.flags() & Flags::Carry() | Alu::parity_flags_for(new_a));
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 5)
constexpr auto ed_instruction<opcode> = Op<"rld", [](Z80 &z80) {
  const auto address = z80.regs().get(RegisterFile::R16::HL);
  const auto ind_hl = z80.read(address);
  const auto prev_a = z80.regs().get(RegisterFile::R8::A);
  const auto new_a = static_cast<std::uint8_t>((prev_a & 0xf0) | ((ind_hl >> 4) & 0xf));
  z80.regs().set(RegisterFile::R8::A, new_a);
  z80.pass_time(4);
  z80.write(address, static_cast<std::uint8_t>(ind_hl << 4 | (prev_a & 0xf)));
  z80.flags(z80.flags() & Flags::Carry() | Alu::parity_flags_for(new_a));
}>{};

template<bool increment, bool repeat>
struct BlockLoadOp {
  static constexpr Mnemonic mnemonic{"ld"s + (increment ? "i" : "d") + (repeat ? "r" : "")};
  static constexpr void execute(Z80 &z80) {
    constexpr std::uint16_t add = increment ? 0x0001 : 0xffff;
    const auto hl = z80.regs().get(RegisterFile::R16::HL);
    z80.regs().set(RegisterFile::R16::HL, hl + add);
    const auto byte = z80.read(hl);
    const auto de = z80.regs().get(RegisterFile::R16::DE);
    z80.regs().set(RegisterFile::R16::DE, de + add);
    z80.write(de, byte);
    z80.pass_time(2);
    // bits 3 and 5 come from the weird value of "byte read + A", where bit 3 goes to flag 5, and bit 1 to flag 3.
    const auto flag_bits = static_cast<std::uint8_t>(byte + z80.regs().get(RegisterFile::R8::A));

    const auto new_bc = static_cast<std::uint16_t>(z80.regs().get(RegisterFile::R16::BC) - 1);
    z80.regs().set(RegisterFile::R16::BC, new_bc);
    const auto preserved_flags = z80.flags() & (Flags::Sign() | Flags::Zero() | Flags::Carry());
    const auto flags_from_bits =
        (flag_bits & 0x08 ? Flags::Flag3() : Flags()) | (flag_bits & 0x02 ? Flags::Flag5() : Flags());
    const auto flags_from_bc = new_bc ? Flags::Overflow() : Flags();
    z80.flags(preserved_flags | flags_from_bits | flags_from_bc);
    if (repeat && new_bc) {
      z80.regs().wz(z80.pc() - 1);
      z80.regs().pc(z80.pc() - 2);
      z80.pass_time(5);
    }
  }
};

template<Opcode opcode>
  requires(opcode.x == 2 && opcode.z == 0)
constexpr auto ed_instruction<opcode> =
    BlockLoadOp<!static_cast<bool>(opcode.y & 1), static_cast<bool>(opcode.y & 2)>{};

template<bool increment, bool repeat>
struct BlockCompareOp {
  static constexpr Mnemonic mnemonic{"cp"s + (increment ? "i" : "d") + (repeat ? "r" : "")};
  static constexpr void execute(Z80 &z80) {
    constexpr std::uint16_t add = increment ? 0x0001 : 0xffff;
    const auto hl = z80.regs().get(RegisterFile::R16::HL);
    z80.regs().set(RegisterFile::R16::HL, hl + add);
    const auto byte = z80.read(hl);

    const auto [result, subtract_flags] = Alu::sub8(z80.regs().get(RegisterFile::R8::A), byte, false);

    z80.pass_time(5);
    // bits 3 and 5 come from the result, where bit 3 goes to flag 5, and bit 1 to flag 3....and where if HF is
    // set we use res--....
    const auto flag_bits = subtract_flags.half_carry() ? result - 1 : result;

    const auto new_bc = static_cast<std::uint16_t>(z80.regs().get(RegisterFile::R16::BC) - 1);
    z80.regs().set(RegisterFile::R16::BC, new_bc);

    constexpr auto from_subtract_mask = Flags::HalfCarry() | Flags::Zero() | Flags::Sign() | Flags::Subtract();
    const auto preserved_flags =
        z80.flags() & ~(Flags::Flag3() | Flags::Flag5() | from_subtract_mask | Flags::Overflow());
    const auto flags_from_bits =
        (flag_bits & 0x08 ? Flags::Flag3() : Flags()) | (flag_bits & 0x02 ? Flags::Flag5() : Flags());
    const auto flags_from_bc = new_bc ? Flags::Overflow() : Flags();
    z80.flags(preserved_flags | flags_from_bits | flags_from_bc | (from_subtract_mask & subtract_flags));
    if (repeat && new_bc && !subtract_flags.zero()) {
      z80.regs().wz(z80.pc() - 1);
      z80.regs().pc(z80.pc() - 2);
      z80.pass_time(5);
    }
  }
};


template<Opcode opcode>
  requires(opcode.x == 2 && opcode.z == 1)
constexpr auto ed_instruction<opcode> =
    BlockCompareOp<!static_cast<bool>(opcode.y & 1), static_cast<bool>(opcode.y & 2)>{};


// TODO otid and inir
template<Opcode opcode>
  requires(opcode.x == 2 && opcode.z == 2)
constexpr auto ed_instruction<opcode> = InvalidInstruction{};
template<Opcode opcode>
  requires(opcode.x == 2 && opcode.z == 3)
constexpr auto ed_instruction<opcode> = InvalidInstruction{};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename SelectInstruction, typename Transform, HlSet hl_set>
constexpr auto generic_table = []<std::size_t... num>(std::index_sequence<num...>) consteval {
  return std::array{Transform::template result<SelectInstruction::template value<Opcode{num, hl_set}>>...};
}(std::make_index_sequence<256>());

struct select_base_instruction {
  template<Opcode opcode>
  static constexpr auto value = instruction<opcode>;
};

struct select_ed_instruction {
  template<Opcode opcode>
  static constexpr auto value = ed_instruction<opcode>;
};

struct select_cb_instruction {
  template<Opcode opcode>
  static constexpr auto value = cb_instruction<opcode>;
};

using execute_ptr_t = void (*)(Z80 &);

template<typename T>
concept OpLike = requires(T t, Z80 &z80) {
  { T::mnemonic } -> std::same_as<const Mnemonic &>;
  { T::execute(z80) };
};

template<typename T>
concept BaseOpLike = OpLike<T> && requires(T t) {
  { T::indirect } -> std::convertible_to<bool>;
};

struct build_description {
  template<auto op>
    requires OpLike<decltype(op)>
  static constexpr auto result = decltype(op)::mnemonic.view();
};

struct build_execute {
  template<auto op>
    requires OpLike<decltype(op)>
  static void result(Z80 &z80) {
    decltype(op)::execute(z80);
  }
};

template<RegisterFile::R16 ix_or_iy>
struct build_execute_ixiy {
  template<auto op>
    requires OpLike<decltype(op)>
  static void result(Z80 &z80) {
    if constexpr (decltype(op)::indirect) {
      const auto address =
          static_cast<std::uint16_t>(z80.regs().get(ix_or_iy) + static_cast<std::int8_t>(z80.read_immediate()));
      // TODO we don't model the fetching of immediates correctly here ld (ix+d), nn but this gets the timing right.
      // heinous hack here.
      static constexpr bool is_immediate = requires { op.is_load_immediate; };
      z80.pass_time(is_immediate ? 2 : 5);
      z80.regs().wz(address);
    }
    op.execute(z80);
  }
};

struct build_execute_hl {
  template<auto op>
    requires OpLike<decltype(op)>
  static void result(Z80 &z80) {
    if constexpr (decltype(op)::indirect) {
      z80.regs().wz(z80.regs().get(RegisterFile::R16::HL));
    }
    op.execute(z80);
  }
};

SPECBOLT_EXPORT template<typename Builder>
constexpr auto table = generic_table<select_base_instruction, Builder, HlSet::Base>;
SPECBOLT_EXPORT template<typename Builder>
constexpr auto dd_table = generic_table<select_base_instruction, Builder, HlSet::Ix>;
SPECBOLT_EXPORT template<typename Builder>
constexpr auto fd_table = generic_table<select_base_instruction, Builder, HlSet::Iy>;
SPECBOLT_EXPORT template<typename Builder>
constexpr auto cb_table = generic_table<select_cb_instruction, Builder, HlSet::Base>;
SPECBOLT_EXPORT template<typename Builder>
constexpr auto ddcb_table = generic_table<select_cb_instruction, Builder, HlSet::Ix>;
SPECBOLT_EXPORT template<typename Builder>
constexpr auto fdcb_table = generic_table<select_cb_instruction, Builder, HlSet::Iy>;
SPECBOLT_EXPORT template<typename Builder>
constexpr auto ed_table = generic_table<select_ed_instruction, Builder, HlSet::Base>;


} // namespace specbolt::v2::impl
