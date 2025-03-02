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
  static constexpr void execute(Z80 &) {}
};

template<std::uint8_t P>
struct Load16ImmOp {
  static constexpr Mnemonic mnemonic{"ld " + std::string(rp_names[P]) + ", $nnnn"};
  static constexpr std::uint8_t read_immediate(Z80 &z80) {
    z80.pass_time(3); // ?
    const auto addr = z80.regs().pc();
    z80.regs().pc(addr + 1);
    return z80.read8(addr);
  }

  static constexpr auto execute(Z80 &z80) {
    z80.regs().set(rp_low[P], read_immediate(z80));
    z80.regs().set(rp_high[P], read_immediate(z80));
  }
};

template<Opcode opcode>
constexpr auto instruction = NopType{}; // TODO once we cover all instructions this will be an error.(maybe not

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.y == 0 && opcode.z == 0)
constexpr auto instruction<opcode> = NopType{};

template<Opcode opcode>
  requires(opcode.x == 0 && opcode.z == 1 && opcode.q == 0)
constexpr auto instruction<opcode> = Load16ImmOp<opcode.p>{};

// struct DjnzOp {
//   static constexpr Mnemonic mnemonic{"djnz $d"};
//   static constexpr std::array micro_ops{
//       MicroOp::delay(1),
//       // MicroOp::read_to_dlatch(),
//       // MicroOp::decrement_b(),
//       // MicroOp::
//   };
// };
//
// template<Opcode opcode>
//   requires(opcode.x == 0 && opcode.y == 2 && opcode.z == 0)
// constexpr auto instruction<opcode> = DjnzOp{};

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

// With huge thanks to Hana!
// https://compiler-explorer.com/z/rr15c7hE9
// https://compiler-explorer.com/z/xqhTe9h7c
// https://compiler-explorer.com/z/EE16rPK9E
// https://compiler-explorer.com/z/8v4Yq8n8T
