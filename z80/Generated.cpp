#include "z80/Generated.hpp"

#include "z80/RegisterFile.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace specbolt {

namespace {
struct MicroOp {
  enum class Type {
    MemRead,
    MemWrite,
    IoRead,
    IoWrite,
  };
  Type type;
  enum class Source {
    Immediate,
    Register,
    Memory,
  };
  Source source;
  RegisterFile::R8 dest;
  int cycles;
  static constexpr MicroOp read_imm(const RegisterFile::R8 dest) {
    return MicroOp{Type::MemRead, Source::Immediate, dest, 3};
  }
};

struct CompiledOp {
  std::string disassembly;
  std::vector<MicroOp> micro_ops;
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
    SourceOp{[](const Opcode &) { return CompiledOp{"nop", {}}; },
        [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 0 && opcode.z == 0; }},
    // SourceOp{const_name("ex af, af'"),
    //     [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 1 && opcode.z == 0; }, {}},
    // SourceOp{
    //     const_name("djnz d"), [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 2 && opcode.z == 0; },
    //     {}},
    // SourceOp{
    //     const_name("jr d"), [](const Opcode &opcode) { return opcode.x == 0 && opcode.y == 3 && opcode.z == 0; },
    //     {}},
    // SourceOp{const_name("jr CC d"), [](const Opcode &opcode) { return opcode.x == 0 && opcode.z == 0; }, {}},
    SourceOp{[](const Opcode &opcode) {
               return CompiledOp{
                   "ld " + std::string(rp_names[opcode.p]) + ", nnnn", {
                                                                           MicroOp::read_imm(rp_low[opcode.p]),
                                                                           MicroOp::read_imm(rp_high[opcode.p]),
                                                                       }};
             },
        [](const Opcode &opcode) { return opcode.x == 0 && opcode.z == 1 && opcode.q == 0; }},
    // SourceOp{rp_p_name("add hl, {}"),
    //     [](const Opcode &opcode) { return opcode.x == 0 && opcode.z == 1 && opcode.q == 1; }, {}},
};

const std::array<CompiledOp, 256> ops = ([] {
  std::array<CompiledOp, 256> result;

  for (auto &&[opcode_num, compiled_op]: std::ranges::views::enumerate(result)) {
    const auto opcode = Opcode{static_cast<std::uint8_t>(opcode_num)};
    compiled_op.disassembly = std::format("?? ({:02x})", opcode_num);
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
  for (auto x = 0uz; x < 256uz; ++x) {
    result[x] = ops[x].disassembly;
  }
  return result;
}

constexpr Z80_Op *compile(const CompiledOp &) {
  // TODO! zomg...how to do this? Heeelllllp!
  static constexpr Z80_Op *nop = +[](Z80 &) -> size_t { return 0; };
  return nop;
}

constexpr std::array<Z80_Op *, 256> make_opcode_ops() {
  std::array<Z80_Op *, 256> result;
  for (auto x = 0uz; x < 256uz; ++x) {
    result[x] = compile(ops[x]);
  }
  return result;
}

} // namespace

const std::array<std::string_view, 256> &base_opcode_names() {
  static auto result = make_opcode_names();
  return result;
}

const std::array<Z80_Op *, 256> &base_opcode_ops() {
  static constexpr auto result = make_opcode_ops();
  return result;
}

} // namespace specbolt
