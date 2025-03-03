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
  std::array<char, 16> storage{};
  size_t len{};
  Mnemonic() = default;
  explicit constexpr Mnemonic(const std::string_view name) {
    std::ranges::copy(name, storage.begin());
    len = name.size();
  }
  [[nodiscard]] constexpr std::string_view view() const { return {storage.data(), len}; }
};

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

constexpr std::array r_names = {"b", "c", "d", "e", "h", "l", "(hl)", "a"};
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
// template<>
// bool cc_check<4>(const Flags flags) {
//   return !flags.parity();
// }
// template<>
// bool cc_check<5>(const Flags flags) {
//   return flags.parity();
// }
// template<>
// bool cc_check<6>(const Flags flags) {
//   return !flags.sign();
// }
// template<>
// bool cc_check<7>(const Flags flags) {
//   return flags.sign();
// }

struct InvalidType {
  static constexpr Mnemonic mnemonic{"???"};
  static constexpr void execute(Z80 &) {}
};

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

const std::array<Z80_Op *, 256> &base_opcode_ops() {
  static constexpr auto result = table<build_evaluate>;
  return result;
}

} // namespace specbolt
