#include "z80/Generated.hpp"

#include "z80/RegisterFile.hpp"
#include "z80/Z80.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <ranges>
#include <string>
#include <string_view>

namespace specbolt {

namespace {
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
  Source source{Source::Immediate};
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
  [[nodiscard]] std::string_view view() const { return {storage.data(), len}; }
};

struct CompiledOp {
  Mnemonic mnemonic;
  std::array<MicroOp, 8> micro_ops{};
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

struct SourceOp {
  CompiledOp (*create)(const Opcode &opcode);
  bool (*matches)(const Opcode &opcode);
};

constexpr std::array rp_names = {"bc", "de", "hl", "sp"};
constexpr std::array rp_high = {RegisterFile::R8::B, RegisterFile::R8::D, RegisterFile::R8::H,
    RegisterFile::R8::A /* TODO NOT THIS should be SP */};
constexpr std::array rp_low = {RegisterFile::R8::C, RegisterFile::R8::E, RegisterFile::R8::L,
    RegisterFile::R8::A /* TODO NOT THIS should be SP */};

// std::function<std::string(const Opcode &opcode)> rp_p_name(const std::string &format) {
//   return [format](const Opcode &opcode) { return std::vformat(format, std::make_format_args(rp[opcode.p])); };
// }

// http://www.z80.info/decoding.htm
constexpr std::array z80_source_ops = {
    // First quadrant, x == 0
    SourceOp{[](const Opcode &) { return CompiledOp{Mnemonic("nop"), {}}; },
        [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 0 && opcode.z == 0; }},
    // SourceOp{Mnemonic("ex af, af'"),
    //     [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 1 && opcode.z == 0; }, {}},
    SourceOp{[](const Opcode &) {
               return CompiledOp{Mnemonic("djnz $d"), {
                                                          MicroOp::delay(1),
                                                          // MicroOp::read_to_dlatch(),
                                                          // MicroOp::decrement_b(),
                                                          // MicroOp::
                                                      }};
             },
        [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 2 && opcode.z == 0; }},
    // SourceOp{
    //     const_name("jr d"), [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 3 && opcode.z == 0; },
    //     {}},
    // SourceOp{const_name("jr CC d"), [](const Opcode &opcode) { return opcode.x == 0 && opcode.z == 0; }, {}},
    SourceOp{[](const Opcode &opcode) {
               return CompiledOp{Mnemonic("ld " + std::string(rp_names[opcode.p]) + ", $nnnn"),
                   {
                       MicroOp::read_imm(rp_low[opcode.p]),
                       MicroOp::read_imm(rp_high[opcode.p]),
                   }};
             },
        [](const Opcode &opcode) { return opcode.x == 0 && opcode.z == 1 && opcode.q == 0; }},
    // SourceOp{rp_p_name("add hl, {}"),
    //     [](const Opcode &opcode) { return opcode.x == 0 && opcode.z == 1 && opcode.q == 1; }, {}},
};

constexpr std::array<CompiledOp, 256> ops = ([] {
  std::array<CompiledOp, 256> result;

  for (auto &&[opcode_num, compiled_op]: std::ranges::views::enumerate(result)) {
    const auto opcode = Opcode{static_cast<std::uint8_t>(opcode_num)};
    // TODO zomg strings at constexpr time...
    // compiled_op.mnemonic = Mnemonic("?? " + std::to_string(opcode_num));
    for (const auto &[create, matches]: z80_source_ops) {
      if (matches(opcode)) {
        compiled_op = create(opcode);
        break;
      }
    }
  }

  return result;
})();

std::array<std::string_view, 256> make_opcode_names() {
  std::array<std::string_view, 256> result;
  for (auto x = 0uz; x < 256uz; ++x)
    result[x] = ops[x].mnemonic.view();
  return result;
}

// template<MicroOp op>
// struct MicroOpCompiler {
//   static std::size_t operator()(Z80 &cpu) {
//     if constexpr (op.type == MicroOp::Type::MemRead) {
//       if constexpr (op.source == MicroOp::Source::Immediate) {
//         cpu.registers().set(op.dest, cpu.memory().read(cpu.registers().pc()));
//       }
//     }
//     return op.cycles;
//   }
// };


// constexpr Z80_Op *compile(const MicroOp micro_op) {
//   constexpr auto op = micro_op;
//   static constexpr Z80_Op *nop = +[](Z80 &) -> size_t { return 0; };
//   switch (micro_op.type) {
//     case MicroOp::Type::MemRead:
//       return +[](Z80 &cpu) -> size_t {
//         cpu.registers().set(op.dest, cpu.memory().read(cpu.registers().pc()));
//         return op.cycles;
//       };
//     default: return nop;
//     // case MicroOp::Type::MemWrite:
//     //   return +[](Z80 &cpu) -> size_t {
//     //     cpu.memory().write(cpu.registers().pc(), cpu.registers().get(micro_op.dest));
//     //     return micro_op.cycles;
//     //   };
//     // case MicroOp::Type::IoRead:
//     //   return +[](Z80 &cpu) -> size_t {
//     //     cpu.registers().set(micro_op.dest, cpu.io().read(cpu.registers().pc()));
//     //     return micro_op.cycles;
//     //   };
//     // case MicroOp::Type::IoWrite:
//     //   return +[](Z80 &cpu) -> size_t {
//     //     cpu.io().write(cpu.registers().pc(), cpu.registers().get(micro_op.dest));
//     //     return micro_op.cycles;
//     //   };
//   }
// }

// constexpr Z80_Op *compile(const CompiledOp &) {
//   // TODO! zomg...how to do this? Heeelllllp!
//   // static constexpr Z80_Op *nop = +[](Z80 &) -> size_t { return 0; };
//   // read_imm
//   // return nop;
//   constexpr auto read_op = MicroOp::read_imm(RegisterFile::R8::A);
//   return &MicroOpCompiler<read_op>::operator();
// }
//
// constexpr std::array<Z80_Op *, 256> make_opcode_ops() {
//   std::array<Z80_Op *, 256> result{};
//   for (auto x = 0uz; x < 256uz; ++x) {
//     result[x] = compile(ops[x]);
//   }
//   return result;
// }

constexpr bool execute(const MicroOp op, Z80 &z80) {
  z80.pass_time(op.cycles);
  switch (op.type) {
    case MicroOp::Type::MemRead: z80.regs().set(op.dest, z80.read8(z80.regs().pc())); break;
    default: break;
  }
  return true;
}

template<CompiledOp Instruction> /*[[gnu::flatten]] */ constexpr void evaluate(Z80 &z80) {
  // c++26 (not yet implemented)
  // template for (constexpr micro_instruction MicroOps: Instruction.micro) {
  //    microexec(micro_opcode_v<MicroOps.op>, c, source_accessor<MicroOps.source>(c),
  //    target_accessor<MicroOps.target>(c));
  // }

  // const auto &[... MicroOps] = Instruction.micro_ops;
  //
  // (microexec(micro_opcode_v<MicroOps.op>, c, source_accessor<MicroOps.source>(c),
  // target_accessor<MicroOps.target>(c)),
  //     ...);
  bool keep_going = true;
  [&]<size_t... Idx>(std::index_sequence<Idx...>) {
    ((keep_going = keep_going ? execute(Instruction.micro_ops[Idx], z80) : false), ...);
  }(std::make_index_sequence<Instruction.micro_ops.size()>());
}

template<std::array Instructions>
consteval auto make_opcode_ops() {
  // C++26:
  // const auto & [...Ops] = Instructions;
  // return std::array{fptr{&evaluate<Ops>}...};
  // before:
  return []<size_t... Idx>(std::index_sequence<Idx...>) { return std::array{&evaluate<Instructions[Idx]>...}; }(
             std::make_index_sequence<Instructions.size()>());
}

} // namespace

const std::array<std::string_view, 256> &base_opcode_names() {
  static auto result = make_opcode_names();
  return result;
}

const std::array<Z80_Op *, 256> &base_opcode_ops() {
  static constexpr auto result = make_opcode_ops<ops>();
  return result;
}

} // namespace specbolt

// https://compiler-explorer.com/z/rr15c7hE9
// https://compiler-explorer.com/z/xqhTe9h7c
