#include "z80/Generated.hpp"

#include "z80/RegisterFile.hpp"
#include "z80/Z80.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

using namespace std::literals;

namespace specbolt {

namespace {

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
  static constexpr auto highlow = RegisterFile::R16::HL;
  static constexpr auto high = RegisterFile::R8::H;
  static constexpr auto low = RegisterFile::R8::L;
};
template<>
struct IndexReg<HlSet::Ix> {
  static constexpr auto name = "ix";
  static constexpr auto highlow = RegisterFile::R16::IX;
  static constexpr auto high = RegisterFile::R8::IXH;
  static constexpr auto low = RegisterFile::R8::IXL;
};
template<>
struct IndexReg<HlSet::Iy> {
  static constexpr auto name = "iy";
  static constexpr auto highlow = RegisterFile::R16::IY;
  static constexpr auto high = RegisterFile::R8::IYH;
  static constexpr auto low = RegisterFile::R8::IYL;
};

struct Mnemonic {
  std::array<char, 15> storage{};
  size_t len{};
  Mnemonic() = default;
  explicit constexpr Mnemonic(const std::string_view name) {
    std::ranges::copy(name, storage.begin());
    len = name.size();
  }
  [[nodiscard]] constexpr std::string_view view() const { return {storage.data(), len}; }
};

// TODO put these on the z80 itself and either:
// - split z80 into two for the in- and out-band
// - or update the Instruction etc code accordingly. as these model time.
constexpr std::uint8_t read_immediate(Z80 &z80) {
  z80.pass_time(3);
  const auto addr = z80.regs().pc();
  z80.regs().pc(addr + 1);
  return z80.read8(addr);
}

constexpr std::uint16_t read_immediate16(Z80 &z80) {
  const auto low = read_immediate(z80);
  const auto high = read_immediate(z80);
  return static_cast<std::uint16_t>(high << 8 | low);
}

constexpr void write(Z80 &z80, const std::uint16_t address, const std::uint8_t byte) {
  z80.pass_time(3);
  z80.write8(address, byte);
}

constexpr std::uint8_t read(Z80 &z80, const std::uint16_t address) {
  z80.pass_time(3);
  return z80.read8(address);
}

constexpr std::uint8_t pop8(Z80 &z80) {
  const auto value = read(z80, z80.regs().sp());
  z80.regs().sp(z80.regs().sp() + 1);
  return value;
}

constexpr std::uint16_t pop16(Z80 &z80) {
  const auto low = pop8(z80);
  const auto high = pop8(z80);
  return static_cast<std::uint16_t>(high << 8 | low);
}

constexpr void push8(Z80 &z80, const std::uint8_t value) {
  z80.regs().sp(z80.regs().sp() - 1);
  write(z80, z80.regs().sp(), value);
}

constexpr void push16(Z80 &z80, const std::uint16_t value) {
  push8(z80, static_cast<std::uint8_t>(value >> 8));
  push8(z80, static_cast<std::uint8_t>(value));
}

// TODO combine with the r_regs below etcetc
constexpr std::string get_r_name(const std::uint8_t index, const HlSet hl_set, const bool no_remap_ixiy_8b = false) {
  constexpr std::array base_r_names{"b", "c", "d", "e", "h", "l", "(hl)", "a", "$nn"};
  constexpr std::array ix_r_names{"b", "c", "d", "e", "ixh", "ixl", "(ix$o)", "a", "$nn"};
  constexpr std::array iy_r_names = {"b", "c", "d", "e", "iyh", "iyl", "(iy$o)", "a", "$nn"};
  constexpr std::array ix_rr_names{"b", "c", "d", "e", "h", "l", "(ix$o)", "a", "$nn"};
  constexpr std::array iy_rr_names = {"b", "c", "d", "e", "h", "l", "(iy$o)", "a", "$nn"};

  switch (hl_set) {
    case HlSet::Base: return base_r_names[index];
    case HlSet::Ix: return no_remap_ixiy_8b ? ix_rr_names[index] : ix_r_names[index];
    case HlSet::Iy: return no_remap_ixiy_8b ? iy_rr_names[index] : iy_r_names[index];
  }
  std::unreachable();
}

constexpr auto is_r_indirect(const std::uint8_t index) { return index == 6; }

template<HlSet hl_set, bool no_remap_ixiy_8b>
constexpr std::array r_regs = {RegisterFile::R8::B, RegisterFile::R8::C, RegisterFile::R8::D, RegisterFile::R8::E,
    RegisterFile::R8::H, RegisterFile::R8::L, RegisterFile::R8::A /* NOT REALLY */, RegisterFile::R8::A};
template<>
constexpr std::array r_regs<HlSet::Ix, false> = {RegisterFile::R8::B, RegisterFile::R8::C, RegisterFile::R8::D,
    RegisterFile::R8::E, RegisterFile::R8::IXH, RegisterFile::R8::IXL, RegisterFile::R8::A /* NOT REALLY */,
    RegisterFile::R8::A};
template<>
constexpr std::array r_regs<HlSet::Iy, false> = {RegisterFile::R8::B, RegisterFile::R8::C, RegisterFile::R8::D,
    RegisterFile::R8::E, RegisterFile::R8::IYH, RegisterFile::R8::IYL, RegisterFile::R8::A /* NOT REALLY */,
    RegisterFile::R8::A};
template<>
constexpr std::array r_regs<HlSet::Ix, true> = {RegisterFile::R8::B, RegisterFile::R8::C, RegisterFile::R8::D,
    RegisterFile::R8::E, RegisterFile::R8::H, RegisterFile::R8::L, RegisterFile::R8::A /* NOT REALLY */,
    RegisterFile::R8::A};
template<>
constexpr std::array r_regs<HlSet::Iy, true> = {RegisterFile::R8::B, RegisterFile::R8::C, RegisterFile::R8::D,
    RegisterFile::R8::E, RegisterFile::R8::H, RegisterFile::R8::L, RegisterFile::R8::A /* NOT REALLY */,
    RegisterFile::R8::A};

template<std::uint8_t index, HlSet hl_set, bool no_remap_ixiy_8b>
struct OperandGetSetter {
  constexpr static uint8_t get(Z80 &z80) { return z80.regs().get(r_regs<hl_set, no_remap_ixiy_8b>[index]); }
  constexpr static void set(Z80 &z80, const std::uint8_t value) {
    z80.regs().set(r_regs<hl_set, no_remap_ixiy_8b>[index], value);
  }
};

// Special case for indirect indexed register.
template<HlSet hl_set, bool no_remap_ixiy_8b>
struct OperandGetSetter<6, hl_set, no_remap_ixiy_8b> {
  constexpr static uint8_t get(Z80 &z80) { return read(z80, z80.regs().wz()); }
  constexpr static void set(Z80 &z80, const std::uint8_t value) { write(z80, z80.regs().wz(), value); }
};

template<HlSet hl_set, bool no_remap_ixiy_8b>
struct OperandGetSetter<8, hl_set, no_remap_ixiy_8b> {
  constexpr static uint8_t get(Z80 &z80) { return read_immediate(z80); };
  constexpr static void set(Z80 &z80, std::uint8_t) = delete;
};

template<std::uint8_t y, HlSet hl_set, bool no_remap_ixiy_8b = false>
constexpr uint8_t get_r(Z80 &z80) {
  return OperandGetSetter<y, hl_set, no_remap_ixiy_8b>::get(z80);
}
template<std::uint8_t y, HlSet hl_set, bool no_remap_ixiy_8b = false>
constexpr void set_r(Z80 &z80, const std::uint8_t value) {
  OperandGetSetter<y, hl_set, no_remap_ixiy_8b>::set(z80, value);
}

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

constexpr std::array cc_names = {"nz", "z", "nc", "c", "po", "pe", "p", "m"};
template<std::uint8_t>
bool cc_check(Flags flags) = delete;
template<>
bool cc_check<0>(const Flags flags) {
  return !flags.zero();
}
template<>
bool cc_check<1>(const Flags flags) {
  return flags.zero();
}
template<>
bool cc_check<2>(const Flags flags) {
  return !flags.carry();
}
template<>
bool cc_check<3>(const Flags flags) {
  return flags.carry();
}
template<>
bool cc_check<4>(const Flags flags) {
  return !flags.parity();
}
template<>
bool cc_check<5>(const Flags flags) {
  return flags.parity();
}
template<>
bool cc_check<6>(const Flags flags) {
  return !flags.sign();
}
template<>
bool cc_check<7>(const Flags flags) {
  return flags.sign();
}

struct MissingInstruction {};

struct InvalidInstruction {
  static constexpr Mnemonic mnemonic{"??"};
  static constexpr void execute(Z80 &) {}
};

struct NopType {
  static constexpr Mnemonic mnemonic{"nop"};
  static constexpr void execute(Z80 &) {}
  static constexpr bool indirect = false;
};

template<std::uint8_t P, HlSet hl_set>
struct Load16ImmOp {
  using TableRp = TableRp<hl_set>;
  static constexpr Mnemonic mnemonic{"ld " + std::string(TableRp::names[P]) + ", $nnnn"};
  static constexpr bool indirect = false;

  static constexpr auto execute(Z80 &z80) {
    // TODO: just go straight to low/high? or is there a visible difference?
    z80.regs().set(TableRp::low[P], read_immediate(z80));
    z80.regs().set(TableRp::high[P], read_immediate(z80));
  }
};

template<Mnemonic mnem, auto op, bool ind = false>
struct SimpleOp {
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

  explicit constexpr Opcode(const std::uint8_t opcode, const HlSet hl_set) :
      x(opcode >> 6), y((opcode >> 3) & 0x7), z(opcode & 0x7), p(y >> 1), q(y & 1), hl_set(hl_set) {}

  [[nodiscard]] constexpr bool is_alu_op() const { return x == 2 || (x == 3 && z == 6); }
  [[nodiscard]] constexpr int alu_op() const { return is_alu_op() ? y : -1; }
  [[nodiscard]] constexpr std::uint8_t alu_input_selector() const { return x == 2 ? z : 8; }
};

template<Opcode opcode>
constexpr auto instruction = MissingInstruction{};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X = 0
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 0 && opcode.z == 0)
constexpr auto instruction<opcode> = NopType{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 1 && opcode.z == 0)
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("ex af, af'"), [](Z80 &z80) { z80.regs().ex(RegisterFile::R16::AF, RegisterFile::R16::AF_); }>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 2 && opcode.z == 0)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("djnz $d"), [](Z80 &z80) {
  z80.pass_time(1);
  const auto offset = static_cast<std::int8_t>(read_immediate(z80));
  const std::uint8_t new_b = z80.regs().get(RegisterFile::R8::B) - 1;
  z80.regs().set(RegisterFile::R8::B, new_b);
  if (new_b == 0)
    return;
  z80.pass_time(5);
  z80.branch(offset);
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 3 && opcode.z == 0)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("jr $d"), [](Z80 &z80) {
  const auto offset = static_cast<std::int8_t>(read_immediate(z80));
  z80.pass_time(5);
  z80.branch(offset);
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && (opcode.y >= 4 && opcode.y <= 7) && opcode.z == 0)
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("jr " + std::string(cc_names[opcode.y - 4]) + " $d"), [](Z80 &z80) {
      const auto offset = static_cast<std::int8_t>(read_immediate(z80));
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
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("add "s + IndexReg<opcode.hl_set>::name + ", " +
                                                       std::string(TableRp<opcode.hl_set>::names[opcode.p])),
    [](Z80 &z80) {
      const auto rhs = z80.regs().get(TableRp<opcode.hl_set>::highlow[opcode.p]);
      const auto [result, flags] = Alu::add16(z80.regs().get(IndexReg<opcode.hl_set>::highlow), rhs, z80.flags());
      z80.regs().set(IndexReg<opcode.hl_set>::highlow, result);
      z80.flags(flags);
      z80.pass_time(7);
    }>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 0)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld (bc), a"),
    [](Z80 &z80) { write(z80, z80.regs().get(RegisterFile::R16::BC), z80.regs().get(RegisterFile::R8::A)); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 1)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld (de), a"),
    [](Z80 &z80) { write(z80, z80.regs().get(RegisterFile::R16::DE), z80.regs().get(RegisterFile::R8::A)); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 2)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld ($nnnn), "s + IndexReg<opcode.hl_set>::name), [](Z80 &z80) {
  const auto address = read_immediate16(z80);
  write(z80, address, z80.regs().get(IndexReg<opcode.hl_set>::low));
  write(z80, address + 1, z80.regs().get(IndexReg<opcode.hl_set>::high));
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 3)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld ($nnnn), a"), [](Z80 &z80) {
  const auto address = read_immediate16(z80);
  write(z80, address, z80.regs().get(RegisterFile::R8::A));
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 0)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld a, (bc)"),
    [](Z80 &z80) { z80.regs().set(RegisterFile::R8::A, read(z80, z80.regs().get(RegisterFile::R16::BC))); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 1)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld a, (de)"),
    [](Z80 &z80) { z80.regs().set(RegisterFile::R8::A, read(z80, z80.regs().get(RegisterFile::R16::DE))); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 2)
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("ld "s + IndexReg<opcode.hl_set>::name + ", ($nnnn)"), [](Z80 &z80) {
      const auto address = read_immediate16(z80);
      z80.regs().set(IndexReg<opcode.hl_set>::low, read(z80, address));
      z80.regs().set(IndexReg<opcode.hl_set>::high, read(z80, address + 1));
    }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 3)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld a, ($nnnn)"), [](Z80 &z80) {
  const auto address = read_immediate16(z80);
  z80.regs().set(RegisterFile::R8::A, read(z80, address));
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 3 && opcode.q == 0)
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("inc " + std::string(TableRp<opcode.hl_set>::names[opcode.p])), [](Z80 &z80) {
      z80.regs().set(
          TableRp<opcode.hl_set>::highlow[opcode.p], z80.regs().get(TableRp<opcode.hl_set>::highlow[opcode.p]) + 1);
      z80.pass_time(2);
    }>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 3 && opcode.q == 1)
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("dec " + std::string(TableRp<opcode.hl_set>::names[opcode.p])), [](Z80 &z80) {
      z80.regs().set(
          TableRp<opcode.hl_set>::highlow[opcode.p], z80.regs().get(TableRp<opcode.hl_set>::highlow[opcode.p]) - 1);
      z80.pass_time(2);
    }>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 4)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("inc " + get_r_name(opcode.y, opcode.hl_set)),
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
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("dec " + get_r_name(opcode.y, opcode.hl_set)),
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
  requires(opcode.x == 0 && opcode.z == 6)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld " + get_r_name(opcode.y, opcode.hl_set) + ", $nn"),
    [](Z80 &z80) {
      const auto value = read_immediate((z80));
      set_r<opcode.y, opcode.hl_set>(z80, value);
    },
    is_r_indirect(opcode.y)>{};

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
constexpr auto instruction<opcode> = FastAluOp<Mnemonic("rlca"), [](const std::uint8_t a, const Flags flags) {
  return Alu::fast_rotate_circular8(a, Alu::Direction::Left, flags);
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 1)
constexpr auto instruction<opcode> = FastAluOp<Mnemonic("rrca"), [](const std::uint8_t a, const Flags flags) {
  return Alu::fast_rotate_circular8(a, Alu::Direction::Right, flags);
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 2)
constexpr auto instruction<opcode> = FastAluOp<Mnemonic("rla"),
    [](const std::uint8_t a, const Flags flags) { return Alu::fast_rotate8(a, Alu::Direction::Left, flags); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 3)
constexpr auto instruction<opcode> = FastAluOp<Mnemonic("rra"),
    [](const std::uint8_t a, const Flags flags) { return Alu::fast_rotate8(a, Alu::Direction::Right, flags); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 4)
constexpr auto instruction<opcode> =
    FastAluOp<Mnemonic("daa"), [](const std::uint8_t a, const Flags flags) { return Alu::daa(a, flags); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 5)
constexpr auto instruction<opcode> =
    FastAluOp<Mnemonic("cpl"), [](const std::uint8_t a, const Flags flags) { return Alu::cpl(a, flags); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 6)
constexpr auto instruction<opcode> =
    FastAluOp<Mnemonic("scf"), [](const std::uint8_t a, const Flags flags) { return Alu::scf(a, flags); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 7 && opcode.y == 7)
constexpr auto instruction<opcode> =
    FastAluOp<Mnemonic("ccf"), [](const std::uint8_t a, const Flags flags) { return Alu::ccf(a, flags); }>{};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X = 1
template<Opcode opcode>
  requires(opcode.x == 1 && !(opcode.y == 6 && opcode.z == 6))
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("ld " + get_r_name(opcode.y, opcode.hl_set, is_r_indirect(opcode.z)) + ", " +
                      get_r_name(opcode.z, opcode.hl_set, is_r_indirect(opcode.y))),
        [](Z80 &z80) {
          set_r<opcode.y, opcode.hl_set, is_r_indirect(opcode.z)>(
              z80, get_r<opcode.z, opcode.hl_set, is_r_indirect(opcode.y)>(z80));
        },
        is_r_indirect(opcode.y) || is_r_indirect(opcode.z)>{};
template<Opcode opcode>
  requires(opcode.x == 1 && opcode.y == 6 && opcode.z == 6)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("halt"), [](Z80 &z80) { z80.halt(); }>{};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X = 2 (and parts of X = 3)
template<Mnemonic mnem, Opcode opcode, auto alu_op>
struct AluOp {
  static constexpr auto mnemonic =
      Mnemonic(std::string(mnem.view()) + " " + get_r_name(opcode.alu_input_selector(), opcode.hl_set));
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
constexpr auto instruction<opcode> = AluOp<Mnemonic("add a,"), opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::add8(lhs, rhs, false); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 1)
constexpr auto instruction<opcode> =
    AluOp<Mnemonic("adc a,"), opcode, [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags flags) {
      return Alu::add8(lhs, rhs, flags.carry());
    }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 2)
constexpr auto instruction<opcode> = AluOp<Mnemonic("sub a,"), opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::sub8(lhs, rhs, false); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 3)
constexpr auto instruction<opcode> =
    AluOp<Mnemonic("sbc a,"), opcode, [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags flags) {
      return Alu::sub8(lhs, rhs, flags.carry());
    }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 4)
constexpr auto instruction<opcode> = AluOp<Mnemonic("and"), opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::and8(lhs, rhs); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 5)
constexpr auto instruction<opcode> = AluOp<Mnemonic("xor"), opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::xor8(lhs, rhs); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 6)
constexpr auto instruction<opcode> = AluOp<Mnemonic("or"), opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::or8(lhs, rhs); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 7)
constexpr auto instruction<opcode> = AluOp<Mnemonic("cp"), opcode,
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::cmp8(lhs, rhs); }>{};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X = 3 (some alu ops are definewd in X = 1 above)
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 0)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ret " + std::string(cc_names[opcode.y])), [](Z80 &z80) {
  z80.pass_time(1);
  if (cc_check<opcode.y>(z80.flags())) {
    const auto return_address = pop16(z80);
    z80.regs().pc(return_address);
  }
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 0)
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("pop " + std::string(TableRp2<opcode.hl_set>::names[opcode.p])), [](Z80 &z80) {
      const auto result = pop16(z80);
      z80.regs().set(TableRp2<opcode.hl_set>::highlow[opcode.p], result);
    }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 1 && opcode.p == 0)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ret"), [](Z80 &z80) {
  const auto return_address = pop16(z80);
  z80.regs().pc(return_address);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 1 && opcode.p == 1)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("exx"), [](Z80 &z80) { z80.regs().exx(); }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 1 && opcode.p == 2)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("jp ("s + IndexReg<opcode.hl_set>::name + ")"),
    [](Z80 &z80) { z80.regs().pc(z80.regs().get(IndexReg<opcode.hl_set>::highlow)); }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 1 && opcode.p == 3)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld sp, "s + IndexReg<opcode.hl_set>::name), [](Z80 &z80) {
  z80.pass_time(2);
  z80.regs().sp(z80.regs().get(IndexReg<opcode.hl_set>::highlow));
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 2)
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("jp " + std::string(cc_names[opcode.y]) + ", $nnnn"), [](Z80 &z80) {
      const auto jump_address = read_immediate16((z80));
      if (cc_check<opcode.y>(z80.flags())) {
        z80.regs().pc(jump_address);
      }
    }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 0)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("jp $nnnn"), [](Z80 &z80) {
  const auto jump_address = read_immediate16((z80));
  z80.regs().pc(jump_address);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 1)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("CB"), [](Z80 &z80) {
  if constexpr (opcode.hl_set == HlSet::Base)
    decode_and_run_cb(z80);
  else if constexpr (opcode.hl_set == HlSet::Ix)
    decode_and_run_ddcb(z80);
  else if constexpr (opcode.hl_set == HlSet::Iy)
    decode_and_run_fdcb(z80);
  else
    std::unreachable();
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 2)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("out ($nn), a"), [](Z80 &z80) {
  const auto a = z80.regs().get(RegisterFile::R8::A);
  const auto port = static_cast<std::uint16_t>(read_immediate(z80) | a << 8);
  z80.pass_time(4); // OUT time (TODO pass time as IO?)
  z80.out(port, a);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 3)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("in a, ($nn)"), [](Z80 &z80) {
  const auto port = static_cast<std::uint16_t>(read_immediate(z80) | z80.regs().get(RegisterFile::R8::A) << 8);
  z80.pass_time(4); // IN time (TODO pass time as IO?)
  z80.regs().set(RegisterFile::R8::A, z80.in(port));
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 4)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ex (sp), "s + IndexReg<opcode.hl_set>::name), [](Z80 &z80) {
  const auto sp_old_low = read(z80, z80.regs().sp());
  z80.pass_time(1);
  const auto sp_old_high = read(z80, z80.regs().sp() + 1);
  write(z80, z80.regs().sp(), z80.regs().get(IndexReg<opcode.hl_set>::low));
  z80.pass_time(2);
  write(z80, z80.regs().sp() + 1, z80.regs().get(IndexReg<opcode.hl_set>::high));
  z80.regs().set(IndexReg<opcode.hl_set>::highlow, static_cast<std::uint16_t>(sp_old_low | sp_old_high << 8));
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 5)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ex de, "s + IndexReg<opcode.hl_set>::name),
    [](Z80 &z80) { z80.regs().ex(RegisterFile::R16::DE, IndexReg<opcode.hl_set>::highlow); }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 6)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("di"), [](Z80 &z80) {
  z80.iff1(false);
  z80.iff2(false);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 7)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ei"), [](Z80 &z80) {
  z80.iff1(true);
  z80.iff2(true);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 4)
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("call " + std::string(cc_names[opcode.y]) + ", $nnnn"), [](Z80 &z80) {
      const auto jump_address = read_immediate16((z80));
      if (cc_check<opcode.y>(z80.flags())) {
        z80.pass_time(1);
        push16(z80, z80.pc());
        z80.regs().pc(jump_address);
      }
    }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 0)
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("push " + std::string(TableRp2<opcode.hl_set>::names[opcode.p])), [](Z80 &z80) {
      z80.pass_time(1);
      push16(z80, z80.regs().get(TableRp2<opcode.hl_set>::highlow[opcode.p]));
    }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 1 && opcode.p == 0)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("call $nnnn"), [](Z80 &z80) {
  const auto jump_address = read_immediate16((z80));
  z80.pass_time(1);
  push16(z80, z80.pc());
  z80.regs().pc(jump_address);
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 1 && opcode.p == 1)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("DD"), [](Z80 &z80) { decode_and_run_dd(z80); }>{};
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 1 && opcode.p == 2)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ED"), [](Z80 &z80) { decode_and_run_ed(z80); }>{};
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 1 && opcode.p == 3)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("FD"), [](Z80 &z80) { decode_and_run_fd(z80); }>{};

constexpr std::array num_table{"0x00", "0x08", "0x10", "0x18", "0x20", "0x28", "0x30", "0x38"};
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 7)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("rst " + std::string(num_table[opcode.y])), [](Z80 &z80) {
  z80.pass_time(1);
  push16(z80, z80.pc());
  z80.regs().pc(opcode.y * 8);
}>{};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CB prefixed opcodes
template<Opcode opcode>
constexpr auto cb_instruction = MissingInstruction{};

template<Mnemonic mnem, Opcode opcode, auto rotate_op>
struct RotateOp {
  static constexpr auto mnemonic = Mnemonic(std::string(mnem.view()) + " " + get_r_name(opcode.z, opcode.hl_set));
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
constexpr auto cb_instruction<opcode> = RotateOp<Mnemonic("rlc"), opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::rotate_circular8(lhs, Alu::Direction::Left); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 1)
constexpr auto cb_instruction<opcode> = RotateOp<Mnemonic("rrc"), opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::rotate_circular8(lhs, Alu::Direction::Right); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 2)
constexpr auto cb_instruction<opcode> = RotateOp<Mnemonic("rl"), opcode,
    [](const std::uint8_t lhs, const Flags flags) { return Alu::rotate8(lhs, Alu::Direction::Left, flags.carry()); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 3)
constexpr auto cb_instruction<opcode> = RotateOp<Mnemonic("rr"), opcode, [](const std::uint8_t lhs, const Flags flags) {
  return Alu::rotate8(lhs, Alu::Direction::Right, flags.carry());
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 4)
constexpr auto cb_instruction<opcode> = RotateOp<Mnemonic("sla"), opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::shift_arithmetic8(lhs, Alu::Direction::Left); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 5)
constexpr auto cb_instruction<opcode> = RotateOp<Mnemonic("sra"), opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::shift_arithmetic8(lhs, Alu::Direction::Right); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 6)
constexpr auto cb_instruction<opcode> = RotateOp<Mnemonic("sll"), opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::shift_logical8(lhs, Alu::Direction::Left); }>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 7)
constexpr auto cb_instruction<opcode> = RotateOp<Mnemonic("srl"), opcode,
    [](const std::uint8_t lhs, const Flags) { return Alu::shift_logical8(lhs, Alu::Direction::Right); }>{};

template<Opcode opcode>
struct BitOp {
  static constexpr auto mnemonic =
      Mnemonic("bit " + std::string(1, '0' + opcode.y) + ", " + get_r_name(opcode.z, opcode.hl_set));
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
  static constexpr auto mnemonic = Mnemonic(
      (opcode.x == 2 ? "res " : "set ") + std::string(1, '0' + opcode.y) + ", " + get_r_name(opcode.z, opcode.hl_set));
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
constexpr auto ed_instruction<opcode> =
    SimpleOp<Mnemonic("in "s + get_r_name(opcode.y, opcode.hl_set) + ", (c)"), [](Z80 &z80) {
      z80.pass_time(4); // IN time (TODO pass time as IO?)
      const auto port = z80.regs().get(RegisterFile::R16::BC);
      const auto result = z80.in(port);
      z80.flags(z80.flags() & Flags::Carry() | Alu::parity_flags_for(result));
      set_r<opcode.y, opcode.hl_set>(z80, result);
    }>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 0 && opcode.y == 6)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("in (c)"), [](Z80 &z80) {
  z80.pass_time(4); // IN time (TODO pass time as IO?)
  const auto port = z80.regs().get(RegisterFile::R16::BC);
  const auto result = z80.in(port);
  z80.flags(z80.flags() & Flags::Carry() | Alu::parity_flags_for(result));
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 1 && opcode.y != 6)
constexpr auto ed_instruction<opcode> =
    SimpleOp<Mnemonic("out (c), "s + get_r_name(opcode.y, opcode.hl_set)), [](Z80 &z80) {
      z80.pass_time(4); // IN time (TODO pass time as IO?)
      z80.out(z80.regs().get(RegisterFile::R16::BC), get_r<opcode.y, opcode.hl_set>(z80));
    }>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 1 && opcode.y == 6)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("out (c), 0x00"), [](Z80 &z80) {
  z80.pass_time(4); // IN time (TODO pass time as IO?)
  z80.out(z80.regs().get(RegisterFile::R16::BC), 0);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 2 && opcode.q == 0)
constexpr auto ed_instruction<opcode> =
    SimpleOp<Mnemonic("sbc hl, "s + TableRp<opcode.hl_set>::names[opcode.p]), [](Z80 &z80) {
      const auto rhs = z80.regs().get(TableRp<opcode.hl_set>::highlow[opcode.p]);
      const auto lhs = z80.regs().get(RegisterFile::R16::HL);
      const auto [result, flags] = Alu::sbc16(lhs, rhs, z80.flags().carry());
      z80.regs().set(RegisterFile::R16::HL, result);
      z80.flags(flags);
      z80.pass_time(7);
    }>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 2 && opcode.q == 1)
constexpr auto ed_instruction<opcode> =
    SimpleOp<Mnemonic("adc hl, "s + TableRp<opcode.hl_set>::names[opcode.p]), [](Z80 &z80) {
      const auto rhs = z80.regs().get(TableRp<opcode.hl_set>::highlow[opcode.p]);
      const auto lhs = z80.regs().get(RegisterFile::R16::HL);
      const auto [result, flags] = Alu::adc16(lhs, rhs, z80.flags().carry());
      z80.regs().set(RegisterFile::R16::HL, result);
      z80.flags(flags);
      z80.pass_time(7);
    }>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 3 && opcode.q == 0)
constexpr auto ed_instruction<opcode> =
    SimpleOp<Mnemonic("ld ($nnnn), "s + TableRp<opcode.hl_set>::names[opcode.p]), [](Z80 &z80) {
      const auto addr = read_immediate16(z80);
      write(z80, addr, z80.regs().get(TableRp<opcode.hl_set>::low[opcode.p]));
      write(z80, addr + 1, z80.regs().get(TableRp<opcode.hl_set>::high[opcode.p]));
    }>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 3 && opcode.q == 1)
constexpr auto ed_instruction<opcode> =
    SimpleOp<Mnemonic("ld "s + TableRp<opcode.hl_set>::names[opcode.p] + ", ($nnnn)"), [](Z80 &z80) {
      const auto addr = read_immediate16(z80);
      z80.regs().set(TableRp<opcode.hl_set>::low[opcode.p], read(z80, addr));
      z80.regs().set(TableRp<opcode.hl_set>::high[opcode.p], read(z80, addr + 1));
    }>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 4)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("neg"), [](Z80 &z80) {
  const auto [result, flags] = Alu::sub8(0, static_cast<std::uint8_t>(z80.regs().get(RegisterFile::R8::A)), false);
  z80.regs().set(RegisterFile::R8::A, result);
  z80.flags(flags);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 5 && opcode.y != 1)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("retn"), [](Z80 &z80) {
  z80.iff1(z80.iff2());
  const auto return_address = pop16(z80);
  z80.regs().pc(return_address);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 5 && opcode.y == 1)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("reti"), [](Z80 &z80) {
  const auto return_address = pop16(z80);
  z80.regs().pc(return_address);
}>{};

constexpr std::array<std::uint8_t, 8> im_table{0, 0, 1, 2, 0, 0, 1, 2};
template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 6)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("im "s + std::string(1, '0' + im_table[opcode.y])),
    [](Z80 &z80) { z80.irq_mode(im_table[opcode.y]); }>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 0)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("ld i, a"), [](Z80 &z80) {
  z80.pass_time(1);
  z80.regs().i(z80.regs().get(RegisterFile::R8::A));
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 1)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("ld r, a"), [](Z80 &z80) {
  z80.pass_time(1);
  z80.regs().r(z80.regs().get(RegisterFile::R8::A));
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 2)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("ld a, i"), [](Z80 &z80) {
  const auto result = z80.regs().i();
  z80.pass_time(1);
  z80.flags(Alu::iff2_flags_for(result, z80.flags(), z80.iff2()));
  z80.regs().set(RegisterFile::R8::A, result);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 3)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("ld a, r"), [](Z80 &z80) {
  const auto result = z80.regs().r();
  z80.pass_time(1);
  z80.flags(Alu::iff2_flags_for(result, z80.flags(), z80.iff2()));
  z80.regs().set(RegisterFile::R8::A, result);
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 4)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("rrd"), [](Z80 &z80) {
  const auto address = z80.regs().get(RegisterFile::R16::HL);
  const auto ind_hl = read(z80, address);
  const auto prev_a = z80.registers().get(RegisterFile::R8::A);
  const auto new_a = static_cast<std::uint8_t>((prev_a & 0xf0) | (ind_hl & 0xf));
  z80.registers().set(RegisterFile::R8::A, new_a);
  z80.pass_time(4);
  write(z80, address, static_cast<std::uint8_t>(ind_hl >> 4 | ((prev_a & 0xf) << 4)));
  z80.flags(z80.flags() & Flags::Carry() | Alu::parity_flags_for(new_a));
}>{};

template<Opcode opcode>
  requires(opcode.x == 1 && opcode.z == 7 && opcode.y == 5)
constexpr auto ed_instruction<opcode> = SimpleOp<Mnemonic("rld"), [](Z80 &z80) {
  const auto address = z80.regs().get(RegisterFile::R16::HL);
  const auto ind_hl = read(z80, address);
  const auto prev_a = z80.registers().get(RegisterFile::R8::A);
  const auto new_a = static_cast<std::uint8_t>((prev_a & 0xf0) | ((ind_hl >> 4) & 0xf));
  z80.registers().set(RegisterFile::R8::A, new_a);
  z80.pass_time(4);
  write(z80, address, static_cast<std::uint8_t>(ind_hl << 4 | (prev_a & 0xf)));
  z80.flags(z80.flags() & Flags::Carry() | Alu::parity_flags_for(new_a));
}>{};

template<Opcode opcode>
  requires(opcode.x == 2 && opcode.z == 0)
constexpr auto ed_instruction<opcode> = InvalidInstruction{};
template<Opcode opcode>
  requires(opcode.x == 2 && opcode.z == 1)
constexpr auto ed_instruction<opcode> = InvalidInstruction{};
template<Opcode opcode>
  requires(opcode.x == 2 && opcode.z == 2)
constexpr auto ed_instruction<opcode> = InvalidInstruction{};
template<Opcode opcode>
  requires(opcode.x == 2 && opcode.z == 3)
constexpr auto ed_instruction<opcode> = InvalidInstruction{};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<template<auto> typename Transform>
constexpr auto table = []<std::size_t... OpcodeNum>(std::index_sequence<OpcodeNum...>) {
  return std::array{Transform<instruction<Opcode{static_cast<std::uint8_t>(OpcodeNum), HlSet::Base}>>::result...};
}(std::make_index_sequence<256>());

template<template<auto> typename Transform>
constexpr auto dd_table = []<std::size_t... OpcodeNum>(std::index_sequence<OpcodeNum...>) {
  return std::array{Transform<instruction<Opcode{static_cast<std::uint8_t>(OpcodeNum), HlSet::Ix}>>::result...};
}(std::make_index_sequence<256>());

template<template<auto> typename Transform>
constexpr auto ed_table = []<std::size_t... OpcodeNum>(std::index_sequence<OpcodeNum...>) {
  return std::array{Transform<ed_instruction<Opcode{static_cast<std::uint8_t>(OpcodeNum), HlSet::Base}>>::result...};
}(std::make_index_sequence<256>());

template<template<auto> typename Transform>
constexpr auto fd_table = []<std::size_t... OpcodeNum>(std::index_sequence<OpcodeNum...>) {
  return std::array{Transform<instruction<Opcode{static_cast<std::uint8_t>(OpcodeNum), HlSet::Iy}>>::result...};
}(std::make_index_sequence<256>());

// TODO hana can we generalise this? - Yes!!! https://compiler-explorer.com/z/dboW4ndfj -
// https://compiler-explorer.com/z/5vE7756hv
template<template<auto> typename Transform>
constexpr auto cb_table = []<std::size_t... OpcodeNum>(std::index_sequence<OpcodeNum...>) {
  return std::array{Transform<cb_instruction<Opcode{static_cast<std::uint8_t>(OpcodeNum), HlSet::Base}>>::result...};
}(std::make_index_sequence<256>());

template<template<auto> typename Transform>
constexpr auto ddcb_table = []<std::size_t... OpcodeNum>(std::index_sequence<OpcodeNum...>) {
  return std::array{Transform<cb_instruction<Opcode{static_cast<std::uint8_t>(OpcodeNum), HlSet::Ix}>>::result...};
}(std::make_index_sequence<256>());

template<template<auto> typename Transform>
constexpr auto fdcb_table = []<std::size_t... OpcodeNum>(std::index_sequence<OpcodeNum...>) {
  return std::array{Transform<cb_instruction<Opcode{static_cast<std::uint8_t>(OpcodeNum), HlSet::Iy}>>::result...};
}(std::make_index_sequence<256>());

template<auto Opcode>
struct build_description {
  static constexpr auto result = Opcode.mnemonic.view();
};

template<auto Opcode>
struct build_evaluate {
  static void result(Z80 &z80) { Opcode.execute(z80); }
};

template<auto Opcode>
struct build_is_indirect {
  static constexpr bool result = Opcode.indirect;
};

void decode_and_run_cb(Z80 &z80) {
  // Fetch the next opcode.
  const auto opcode = z80.read8(z80.pc());
  z80.regs().pc(z80.pc() + 1);
  // TODO does refresh in here...
  z80.pass_time(4); // Decode...
  if (cb_table<build_is_indirect>[opcode]) { // TODO can de-dupe with HL IX IY? maybe?
    z80.regs().wz(z80.regs().get(RegisterFile::R16::HL));
  }
  // Dispatch and run.
  cb_table<build_evaluate>[opcode](z80);
}

void decode_and_run_ed(Z80 &z80) {
  // Fetch the next opcode.
  const auto opcode = read_immediate(z80);
  // TODO does refresh in here...
  z80.pass_time(1);
  // Dispatch and run.
  ed_table<build_evaluate>[opcode](z80);
}

void decode_and_run_dd(Z80 &z80) {
  // Fetch the next opcode.
  const auto opcode = read_immediate(z80);
  // TODO does refresh in here...
  z80.pass_time(1);
  if (table<build_is_indirect>[opcode]) { // TODO can de-dupe with IX IY
    const auto address = static_cast<std::uint16_t>(z80.regs().get(RegisterFile::R16::IX) + read_immediate(z80));
    // TODO we don't model the fetching of immediates correctly here ld (ix+d), nn but this gets the timing right
    z80.pass_time(opcode == 0x36 ? 2 : 5);
    z80.regs().wz(address);
  }
  // Dispatch and run.
  dd_table<build_evaluate>[opcode](z80);
}

void decode_and_run_fd(Z80 &z80) {
  // Fetch the next opcode.
  const auto opcode = read_immediate(z80);
  z80.pass_time(1);
  // TODO does refresh in here...
  if (table<build_is_indirect>[opcode]) { // TODO can de-dupe with IX IY
    const auto address = static_cast<std::uint16_t>(z80.regs().get(RegisterFile::R16::IY) + read_immediate(z80));
    // TODO we don't model the fetching of immediates correctly here ld (ix+d), nn but this gets the timing right
    z80.pass_time(opcode == 0x36 ? 2 : 5);
    z80.regs().wz(address);
  }
  // Dispatch and run.
  fd_table<build_evaluate>[opcode](z80);
}

void decode_and_run_ddcb(Z80 &z80) {
  const auto offset = static_cast<std::int8_t>(read_immediate(z80));
  z80.pass_time(1); // TODO this?
  const auto address = static_cast<std::uint16_t>(z80.regs().get(RegisterFile::R16::IX) + offset);
  z80.regs().wz(address);
  // Fetch the next opcode.
  const auto opcode = read_immediate(z80);
  // TODO does refresh in here...
  z80.pass_time(1);
  // Dispatch and run.
  ddcb_table<build_evaluate>[opcode](z80);
}

void decode_and_run_fdcb(Z80 &z80) {
  const auto offset = static_cast<std::int8_t>(read_immediate(z80));
  z80.pass_time(1); // TODO this?
  const auto address = static_cast<std::uint16_t>(z80.regs().get(RegisterFile::R16::IY) + offset);
  z80.regs().wz(address);
  // Fetch the next opcode.
  const auto opcode = read_immediate(z80);
  // TODO does refresh in here...
  z80.pass_time(1);
  // Dispatch and run.
  fdcb_table<build_evaluate>[opcode](z80);
}

// TODO the fdcb and ddcb tables miss out on the duplicated encodings and the `res 0,(ix+d,b)` type instructions.
//   Hopefully won't matter for now...

} // namespace

void decode_and_run(Z80 &z80) {
  // Fetch the first opcode.
  const auto opcode = z80.read8(z80.pc());
  z80.regs().pc(z80.pc() + 1);
  z80.pass_time(4); // Decode...
  if (table<build_is_indirect>[opcode])
    z80.regs().wz(z80.regs().get(RegisterFile::R16::HL));
  // Dispatch and run.
  table<build_evaluate>[opcode](z80);
}

Disassembled disassemble(const Memory &memory, std::uint16_t address) {
  const auto initial_address = address;
  auto opcode = memory.read(address++);
  auto disassembly = std::string(table<build_description>[opcode]);
  std::optional<std::int8_t> offset{};
  switch (opcode) {
    case 0xcb:
      opcode = memory.read(address++);
      disassembly = std::string(cb_table<build_description>[opcode]);
      break;
    case 0xdd:
      opcode = memory.read(address++);
      if (opcode == 0xcb) {
        offset = static_cast<std::int8_t>(memory.read(address++));
        opcode = memory.read(address++);
        disassembly = std::string(ddcb_table<build_description>[opcode]);
      }
      else
        disassembly = std::string(dd_table<build_description>[opcode]);
      break;
    case 0xed:
      opcode = memory.read(address++);
      disassembly = std::string(ed_table<build_description>[opcode]);
      break;
    case 0xfd:
      opcode = memory.read(address++);
      if (opcode == 0xcb) {
        offset = static_cast<std::int8_t>(memory.read(address++));
        opcode = memory.read(address++);
        disassembly = std::string(fdcb_table<build_description>[opcode]);
      }
      else
        disassembly = std::string(fd_table<build_description>[opcode]);
      break;
    default: break;
  }
  if (const auto pos = disassembly.find("$o"); pos != std::string::npos) {
    if (!offset)
      offset = static_cast<std::int8_t>(memory.read(address++));
    disassembly = std::format("{}{}0x{:02x}{}", disassembly.substr(0, pos), *offset < 0 ? "-" : "+", std::abs(*offset),
        disassembly.substr(pos + 2));
  }
  if (const auto pos = disassembly.find("$nnnn"); pos != std::string::npos) {
    disassembly =
        std::format("{}0x{:04x}{}", disassembly.substr(0, pos), memory.read16(address), disassembly.substr(pos + 5));
    address += 2;
  }
  if (const auto pos = disassembly.find("$nn"); pos != std::string::npos) {
    disassembly =
        std::format("{}0x{:02x}{}", disassembly.substr(0, pos), memory.read(address++), disassembly.substr(pos + 3));
  }
  if (const auto pos = disassembly.find("$d"); pos != std::string::npos) {
    const auto displacement = static_cast<std::int8_t>(memory.read(address++));
    disassembly = std::format("{}0x{:02x}{}", disassembly.substr(0, pos),
        static_cast<std::uint16_t>(address + displacement), disassembly.substr(pos + 2));
  }
  return {disassembly, static_cast<std::size_t>(address - initial_address)};
}

std::bitset<256> is_indirect_for_testing() {
  std::bitset<256> result{};
  for (std::size_t i = 0; i < 256; ++i)
    result.set(i, table<build_is_indirect>[i]);
  return result;
}


} // namespace specbolt
