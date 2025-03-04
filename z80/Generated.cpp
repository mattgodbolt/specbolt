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

struct Mnemonic {
  std::array<char, 14> storage{};
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

constexpr std::array r_names = {"b", "c", "d", "e", "h", "l", "(hl)", "a", "$nn"};
constexpr std::array r_regs = {RegisterFile::R8::B, RegisterFile::R8::C, RegisterFile::R8::D, RegisterFile::R8::E,
    RegisterFile::R8::H, RegisterFile::R8::L, RegisterFile::R8::A /* NOT REALLY */, RegisterFile::R8::A};
template<std::uint8_t y>
constexpr uint8_t get_r(Z80 &z80) {
  return z80.regs().get(r_regs[y]);
}
template<>
constexpr uint8_t get_r<6>(Z80 &z80) {
  return read(z80, z80.regs().get(RegisterFile::R16::HL));
}
template<>
constexpr uint8_t get_r<8>(Z80 &z80) {
  return read_immediate(z80);
}
template<std::uint8_t y>
constexpr void set_r(Z80 &z80, const std::uint8_t value) {
  z80.regs().set(r_regs[y], value);
}
template<>
constexpr void set_r<6>(Z80 &z80, const std::uint8_t value) {
  write(z80, z80.regs().get(RegisterFile::R16::HL), value);
}

constexpr std::array rp_names = {"bc", "de", "hl", "sp"};
constexpr std::array rp_high = {RegisterFile::R8::B, RegisterFile::R8::D, RegisterFile::R8::H, RegisterFile::R8::SPH};
constexpr std::array rp_low = {RegisterFile::R8::C, RegisterFile::R8::E, RegisterFile::R8::L, RegisterFile::R8::SPL};
constexpr std::array rp_highlow = {
    RegisterFile::R16::BC, RegisterFile::R16::DE, RegisterFile::R16::HL, RegisterFile::R16::SP};
constexpr std::array rp2_names = {"bc", "de", "hl", "af"};
constexpr std::array rp2_highlow = {
    RegisterFile::R16::BC, RegisterFile::R16::DE, RegisterFile::R16::HL, RegisterFile::R16::AF};

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

struct InvalidType {};

struct NopType {
  static constexpr Mnemonic mnemonic{"nop"};
  static constexpr void execute(Z80 &) {}
};

template<std::uint8_t P>
struct Load16ImmOp {
  static constexpr Mnemonic mnemonic{"ld " + std::string(rp_names[P]) + ", $nnnn"};

  static constexpr auto execute(Z80 &z80) {
    // TODO: just go straight to low/high? or is there a visible difference?
    z80.regs().set(rp_low[P], read_immediate(z80));
    z80.regs().set(rp_high[P], read_immediate(z80));
  }
};

template<Mnemonic mnem, auto op>
struct SimpleOp {
  static constexpr auto mnemonic = mnem;
  static constexpr auto execute = op;
};

// http://www.z80.info/decoding.htm
struct Opcode {
  std::uint8_t x;
  std::uint8_t y;
  std::uint8_t z;
  std::uint8_t p;
  std::uint8_t q;

  explicit constexpr Opcode(const std::uint8_t opcode) :
      x(opcode >> 6), y((opcode >> 3) & 0x7), z(opcode & 0x7), p(y >> 1), q(y & 1) {}

  constexpr bool is_alu_op() const { return x == 2 || (x == 3 && z == 6); }
  constexpr int alu_op() const { return is_alu_op() ? y : -1; }
  constexpr std::uint8_t alu_input_selector() const { return x == 2 ? z : 8; }
};

template<Opcode opcode>
constexpr auto instruction = InvalidType{}; // TODO once we cover all instructions this will be an error.(maybe not

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
constexpr auto instruction<opcode> = Load16ImmOp<opcode.p>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 1 && opcode.q == 1)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("add hl, " + std::string(rp_names[opcode.p])), [](Z80 &z80) {
  const auto rhs = z80.regs().get(rp_highlow[opcode.p]);
  const auto [result, flags] = Alu::add16(z80.regs().get(RegisterFile::R16::HL), rhs, z80.flags());
  z80.regs().set(RegisterFile::R16::HL, result);
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
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld ($nnnn), hl"), [](Z80 &z80) {
  const auto address = read_immediate16(z80);
  write(z80, address, z80.regs().get(RegisterFile::R8::L));
  write(z80, address + 1, z80.regs().get(RegisterFile::R8::H));
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
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld hl, ($nnnn)"), [](Z80 &z80) {
  const auto address = read_immediate16(z80);
  z80.regs().set(RegisterFile::R8::L, read(z80, address));
  z80.regs().set(RegisterFile::R8::H, read(z80, address + 1));
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 3)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld a, ($nnnn)"), [](Z80 &z80) {
  const auto address = read_immediate16(z80);
  z80.regs().set(RegisterFile::R8::A, read(z80, address));
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 3 && opcode.q == 0)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("inc " + std::string(rp_names[opcode.p])), [](Z80 &z80) {
  z80.regs().set(rp_highlow[opcode.p], z80.regs().get(rp_highlow[opcode.p]) + 1);
  z80.pass_time(2);
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 3 && opcode.q == 1)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("dec " + std::string(rp_names[opcode.p])), [](Z80 &z80) {
  z80.regs().set(rp_highlow[opcode.p], z80.regs().get(rp_highlow[opcode.p]) - 1);
  z80.pass_time(2);
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 4)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("inc " + std::string(r_names[opcode.y])), [](Z80 &z80) {
  if constexpr (opcode.y == 6) // TODO can we better generalise?
    z80.pass_time(1);
  const auto rhs = get_r<opcode.y>(z80);
  const auto [result, flags] = Alu::inc8(rhs, z80.flags());
  set_r<opcode.y>(z80, result);
  z80.flags(flags);
}>{};
template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 5)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("dec " + std::string(r_names[opcode.y])), [](Z80 &z80) {
  if constexpr (opcode.y == 6) // TODO can we better generalise?
    z80.pass_time(1);
  const auto rhs = get_r<opcode.y>(z80);
  const auto [result, flags] = Alu::dec8(rhs, z80.flags());
  set_r<opcode.y>(z80, result);
  z80.flags(flags);
}>{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 6)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld " + std::string(r_names[opcode.y]) + ", $nn"), [](Z80 &z80) {
  const auto value = read_immediate((z80));
  set_r<opcode.y>(z80, value);
}>{};

template<Mnemonic mnem, auto alu_op>
struct FastAluOp {
  static constexpr Mnemonic mnemonic = mnem;
  static constexpr void execute(Z80 &z80) {
    const auto [result, flags] = alu_op(z80.regs().get(RegisterFile::R8::A), z80.flags());
    z80.regs().set(RegisterFile::R8::A, result);
    z80.flags(flags);
  }
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
    SimpleOp<Mnemonic("ld " + std::string(r_names[opcode.y]) + ", " + std::string(r_names[opcode.z])),
        [](Z80 &z80) { set_r<opcode.y>(z80, get_r<opcode.z>(z80)); }>{};
template<Opcode opcode>
  requires(opcode.x == 1 && opcode.y == 6 && opcode.z == 6)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("halt"), [](Z80 &z80) { z80.halt(); }>{};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X = 2 (and parts of X = 3)
template<Mnemonic mnem, std::uint8_t z, auto alu_op>
struct AluOp {
  static constexpr auto mnemonic = Mnemonic(std::string(mnem.view()) + " " + r_names[z]);
  static constexpr void execute(Z80 &z80) {
    const auto [result, flags] = alu_op(z80.regs().get(RegisterFile::R8::A), get_r<z>(z80), z80.flags());
    z80.regs().set(RegisterFile::R8::A, result);
    z80.flags(flags);
  }
};

template<Opcode opcode>
  requires(opcode.alu_op() == 0)
constexpr auto instruction<opcode> = AluOp<Mnemonic("add a,"), opcode.alu_input_selector(),
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::add8(lhs, rhs, false); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 1)
constexpr auto instruction<opcode> = AluOp<Mnemonic("adc a,"), opcode.alu_input_selector(),
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags flags) {
      return Alu::add8(lhs, rhs, flags.carry());
    }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 2)
constexpr auto instruction<opcode> = AluOp<Mnemonic("sub a,"), opcode.alu_input_selector(),
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::sub8(lhs, rhs, false); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 3)
constexpr auto instruction<opcode> = AluOp<Mnemonic("sbc a,"), opcode.alu_input_selector(),
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags flags) {
      return Alu::sub8(lhs, rhs, flags.carry());
    }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 4)
constexpr auto instruction<opcode> = AluOp<Mnemonic("and"), opcode.alu_input_selector(),
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::and8(lhs, rhs); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 5)
constexpr auto instruction<opcode> = AluOp<Mnemonic("xor"), opcode.alu_input_selector(),
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::xor8(lhs, rhs); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 6)
constexpr auto instruction<opcode> = AluOp<Mnemonic("or"), opcode.alu_input_selector(),
    [](const std::uint8_t lhs, const std::uint8_t rhs, const Flags) { return Alu::or8(lhs, rhs); }>{};
template<Opcode opcode>
  requires(opcode.alu_op() == 7)
constexpr auto instruction<opcode> = AluOp<Mnemonic("cp"), opcode.alu_input_selector(),
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
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("pop " + std::string(rp2_names[opcode.p])), [](Z80 &z80) {
  const auto result = pop16(z80);
  z80.regs().set(rp2_highlow[opcode.p], result);
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
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("jp (hl)"), [](Z80 &z80) { z80.regs().pc(z80.regs().get(RegisterFile::R16::HL)); }>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 1 && opcode.q == 1 && opcode.p == 3)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ld sp, hl"), [](Z80 &z80) {
  z80.pass_time(2);
  z80.regs().sp(z80.regs().get(RegisterFile::R16::HL));
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
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("CB"), [](Z80 &) {}>{};

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
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ex (sp), hl"), [](Z80 &z80) {
  const auto sp_old_low = read(z80, z80.regs().sp());
  z80.pass_time(1);
  const auto sp_old_high = read(z80, z80.regs().sp() + 1);
  write(z80, z80.regs().sp(), z80.regs().get(RegisterFile::R8::L));
  z80.pass_time(2);
  write(z80, z80.regs().sp() + 1, z80.regs().get(RegisterFile::R8::H));
  z80.regs().set(RegisterFile::R16::HL, static_cast<std::uint16_t>(sp_old_low | sp_old_high << 8));
}>{};

template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 3 && opcode.y == 5)
constexpr auto instruction<opcode> =
    SimpleOp<Mnemonic("ex de, hl"), [](Z80 &z80) { z80.regs().ex(RegisterFile::R16::DE, RegisterFile::R16::HL); }>{};

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
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("push " + std::string(rp2_names[opcode.p])), [](Z80 &z80) {
  z80.pass_time(1);
  push16(z80, z80.regs().get(rp2_highlow[opcode.p]));
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
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("DD"), [](Z80 &) {}>{};
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 1 && opcode.p == 2)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("ED"), [](Z80 &) {}>{};
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 5 && opcode.q == 1 && opcode.p == 3)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("FD"), [](Z80 &) {}>{};

constexpr std::array num_table{"0x00", "0x08", "0x10", "0x18", "0x20", "0x28", "0x30", "0x38"};
template<Opcode opcode>
  requires(opcode.x == 3 && opcode.z == 7)
constexpr auto instruction<opcode> = SimpleOp<Mnemonic("rst " + std::string(num_table[opcode.y])), [](Z80 &z80) {
  z80.pass_time(1);
  push16(z80, z80.pc());
  z80.regs().pc(opcode.y * 8);
}>{};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<template<auto> typename Transform>
constexpr auto table = []<std::size_t... OpcodeNum>(std::index_sequence<OpcodeNum...>) {
  return std::array{Transform<instruction<Opcode{static_cast<std::uint8_t>(OpcodeNum)}>>::result...};
}(std::make_index_sequence<256>());

template<auto Opcode>
struct build_description {
  static constexpr auto result = Opcode.mnemonic.view();
};

template<auto Opcode>
struct build_evaluate {
  static void result(Z80 &z80) { Opcode.execute(z80); }
};

} // namespace

const std::array<std::string_view, 256> &base_opcode_names() {
  static auto result = table<build_description>;
  return result;
}

void decode_and_run(Z80 &z80) {
  // Fetch the first opcode.
  const auto opcode = z80.read8(z80.pc());
  z80.regs().pc(z80.pc() + 1);
  z80.pass_time(4); // Decode...
  // Dispatch and run.
  table<build_evaluate>[opcode](z80);
}

} // namespace specbolt
