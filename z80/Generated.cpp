#include "z80/Generated.hpp"

#include "z80/RegisterFile.hpp"
#include "z80/Z80.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <ranges>
#include <string>
#include <string_view>

using namespace std::literals;
namespace specbolt {

namespace {
// TODO! use bespoke types for each microop instead of "type" etc. time can pass and the "framework"? or just write
// instructions out longhand? Harder to generalise? maybe? what do I lose avoiding microops?
struct MicroOp {
  enum class Type {
    Nop,
    MemRead,
    MemWrite,
    IoRead,
    IoWrite,
  };
  Type type{Type::Nop};
  enum class Source {
    None,
    Immediate,
    Register,
    Memory,
  };
  Source source{Source::None};
  RegisterFile::R8 dest;
  std::size_t cycles;
  static constexpr MicroOp read_imm(const RegisterFile::R8 dest) {
    return MicroOp{Type::MemRead, Source::Immediate, dest, 3};
  }
  static constexpr MicroOp delay(const std::size_t cycles) {
    return MicroOp{Type::Nop, Source::None, RegisterFile::R8::A /* todo not this */, cycles};
  }
};

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

struct Opcode {
  std::uint8_t x;
  std::uint8_t y;
  std::uint8_t z;
  std::uint8_t p;
  std::uint8_t q;

  explicit constexpr Opcode(const std::uint8_t opcode) :
      x(opcode >> 6), y((opcode >> 3) & 0x7), z(opcode & 0x7), p(y >> 1), q(y & 1) {}
};

constexpr std::array rp_names = {"bc", "de", "hl", "sp"};
constexpr std::array rp_high = {RegisterFile::R8::B, RegisterFile::R8::D, RegisterFile::R8::H, RegisterFile::R8::SPH};
constexpr std::array rp_low = {RegisterFile::R8::C, RegisterFile::R8::E, RegisterFile::R8::L, RegisterFile::R8::SPL};

struct NopType {
  static constexpr Mnemonic mnemonic{"nop"};
  static constexpr std::array<MicroOp, 0> micro_ops{};
};

template<std::uint8_t P>
struct Load16ImmOp {
  static constexpr Mnemonic mnemonic{"ld " + std::string(rp_names[P]) + ", $nnnn"};
  static constexpr std::array micro_ops{
      MicroOp::read_imm(rp_low[P]),
      MicroOp::read_imm(rp_high[P]),
  };
};

template<Opcode opcode>
constexpr auto instruction = NopType{}; // TODO once we cover all instructions this will be an error.(maybe not

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 0 && opcode.z == 0)
constexpr auto instruction<opcode> = NopType{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 1 && opcode.q == 0)
constexpr auto instruction<opcode> = Load16ImmOp<opcode.p>{};

struct DjnzOp {
  static constexpr Mnemonic mnemonic{"djnz $d"};
  static constexpr std::array micro_ops{
      MicroOp::delay(1),
      // MicroOp::read_to_dlatch(),
      // MicroOp::decrement_b(),
      // MicroOp::
  };
};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 2 && opcode.z == 0)
constexpr auto instruction<opcode> = DjnzOp{};

// http://www.z80.info/decoding.htm
// constexpr std::array z80_source_ops = {
//     // First quadrant, x == 0
//     SourceOp{[](const Opcode &) { return CompiledOp{Mnemonic("nop"), {}}; },
//         [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 0 && opcode.z == 0; }},
//     // SourceOp{Mnemonic("ex af, af'"),
//     //     [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 1 && opcode.z == 0; }, {}},
//     SourceOp{[](const Opcode &) {
//                return CompiledOp{Mnemonic("djnz $d"), {
//                                                           MicroOp::delay(1),
//                                                           // MicroOp::read_to_dlatch(),
//                                                           // MicroOp::decrement_b(),
//                                                           // MicroOp::
//                                                       }};
//              },
//         [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 2 && opcode.z == 0; }},
//     // SourceOp{
//     //     const_name("jr d"), [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 3 && opcode.z == 0; },
//     //     {}},
//     // SourceOp{const_name("jr CC d"), [](const Opcode &opcode) { return opcode.x == 0 && opcode.z == 0; }, {}},
//     SourceOp{[](const Opcode &opcode) {
//                return CompiledOp{Mnemonic("ld " + std::string(rp_names[opcode.p]) + ", $nnnn"),
//                    {
//                        MicroOp::read_imm(rp_low[opcode.p]),
//                        MicroOp::read_imm(rp_high[opcode.p]),
//                    }};
//              },
//         [](const Opcode &opcode) { return opcode.x == 0 && opcode.z == 1 && opcode.q == 0; }},
//     // SourceOp{rp_p_name("add hl, {}"),
//     //     [](const Opcode &opcode) { return opcode.x == 0 && opcode.z == 1 && opcode.q == 1; }, {}},
// };


template<MicroOp::Source source>
struct SourceGetter;
template<>
struct SourceGetter<MicroOp::Source::Immediate> {
  static constexpr std::uint8_t operator()(Z80 &z80) {
    const auto addr = z80.regs().pc();
    z80.regs().pc(addr + 1);
    return z80.read8(addr);
  }
};

template<>
struct SourceGetter<MicroOp::Source::None> {
  static constexpr std::uint8_t operator()(Z80 &) { return 0; }
};

constexpr bool execute(const MicroOp op, auto rhs, Z80 &z80) {
  z80.pass_time(op.cycles);
  switch (op.type) {
    case MicroOp::Type::MemRead: z80.regs().set(op.dest, rhs); break;
    default: break;
  }
  return true;
}

template<auto Instruction>
[[gnu::flatten]] constexpr void evaluate(Z80 &z80) {
  bool keep_going = true;
  [&]<size_t... Idx>(std::index_sequence<Idx...>) {
    ((keep_going = keep_going ? execute(Instruction.micro_ops[Idx],
                                    SourceGetter<Instruction.micro_ops[Idx].source>{}(z80), z80)
                              : false),
        ...);
  }(std::make_index_sequence<Instruction.micro_ops.size()>());
}
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
  // static constexpr auto result = static_cast<Z80_Op *>(&evaluate<Opcode>);
  static void result(Z80 &z80) { evaluate<Opcode>(z80); }
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

// With huge thanks to Hana!
// https://compiler-explorer.com/z/rr15c7hE9
// https://compiler-explorer.com/z/xqhTe9h7c
// https://compiler-explorer.com/z/EE16rPK9E
// https://compiler-explorer.com/z/8v4Yq8n8T
