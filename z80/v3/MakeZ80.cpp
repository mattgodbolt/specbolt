#include <array>
#include <fstream>
#include <iostream>
#include <print>
#include <vector>

using namespace std::literals;

namespace {

struct RegisterSet {
  const char *index_reg;
  const char *index_reg_low;
  const char *index_reg_high;
  std::array<std::string_view, 4> rp;
  std::array<std::string_view, 4> rp_low;
  std::array<std::string_view, 4> rp_high;
  std::array<std::string_view, 8> r;
};
constexpr RegisterSet base_set{.index_reg = "hl",
    .index_reg_low = "l",
    .index_reg_high = "h",
    .rp = {"bc", "de", "hl", "sp"},
    .rp_low = {"c", "e", "l", "spl"},
    .rp_high = {"b", "d", "h", "sph"},
    .r = {"b", "c", "d", "e", "h", "l", "(hl)", "a"}};

std::string upper(const std::string_view input_v) {
  std::string input(input_v);
  std::transform(input.begin(), input.end(), input.begin(), ::toupper);
  return input;
}

constexpr std::array cc_names = {"nz", "z", "nc", "c", "po", "pe", "p", "m"};

// http://www.z80.info/decoding.htm
struct Opcode {
  std::uint8_t x;
  std::uint8_t y;
  std::uint8_t z;
  std::uint8_t p;
  std::uint8_t q;
  const RegisterSet &reg_set;

  explicit constexpr Opcode(const std::size_t opcode, const RegisterSet &reg_set) :
      x(static_cast<std::uint8_t>(opcode) >> 6), y((static_cast<std::uint8_t>(opcode) >> 3) & 0x7),
      z(static_cast<std::uint8_t>(opcode) & 0x7), p(y >> 1), q(y & 1), reg_set(reg_set) {
    if (opcode >= 0x100)
      throw std::runtime_error("Bad opcode");
  }

  [[nodiscard]] constexpr bool is_alu_op() const { return x == 2 || (x == 3 && z == 6); }
  [[nodiscard]] constexpr int alu_op() const { return is_alu_op() ? y : -1; }
  [[nodiscard]] constexpr std::uint8_t alu_input_selector() const { return x == 2 ? z : 8; }
};

struct Op {
  std::string name;
  std::vector<std::string> code;
  bool indirect{false};
  bool is_load_immediate{false};
};

Op match_op(const Opcode opcode) {
  if (opcode.x == 0 && opcode.y == 0 && opcode.z == 0)
    return {"nop", {}};

  if (opcode.x == 0 && opcode.y == 1 && opcode.z == 0)
    return {"ex af, af'", {"regs_.ex(R16::AF, R16::AF_);"s}};

  if (opcode.x == 0 && opcode.z == 1 && opcode.q == 0) {
    return {std::format("ld {}, $nnnn", opcode.reg_set.rp[opcode.p]),
        {
            std::format("set(R8::{}, read_immediate());", upper(opcode.reg_set.rp_low[opcode.p])),
            std::format("set(R8::{}, read_immediate());", upper(opcode.reg_set.rp_high[opcode.p])),
        }};
  }

  if (opcode.x == 0 && opcode.y == 2 && opcode.z == 0)
    return {"djnz $d", {
                           "pass_time(1);",
                           "const auto offset = static_cast<std::int8_t>(read_immediate());",
                           "const std::uint8_t new_b = get(R8::B) - 1;",
                           "set(R8::B, new_b);",
                           "if (new_b == 0) return;",
                           "pass_time(5);",
                           "branch(offset);",
                       }};

  if (opcode.x == 0 && opcode.y == 3 && opcode.z == 0)
    return {"jr $d", {
                         "const auto offset = static_cast<std::int8_t>(read_immediate());",
                         "pass_time(5);",
                         "branch(offset);",
                     }};

  if (opcode.x == 0 && (opcode.y >= 4 && opcode.y <= 7) && opcode.z == 0)
    return {std::format("jr {} $d", cc_names[opcode.y - 4]),
        {
            "const auto offset = static_cast<std::int8_t>(read_immediate());",
            std::format("if (check_{}()) {{", cc_names[opcode.y - 4]),
            "    pass_time(5);",
            "    branch(offset);",
            "}",
        }};

  if (opcode.x == 0 && opcode.z == 1 && opcode.q == 1)
    return {std::format("add {}, {}", opcode.reg_set.index_reg, opcode.reg_set.rp[opcode.p]),
        {std::format("const auto rhs = get(R16::{});", upper(opcode.reg_set.rp[opcode.p])),
            std::format("const auto [result, new_flags] = Alu::add16(get(R16::{}), rhs, flags());",
                upper(opcode.reg_set.index_reg)),
            std::format("set(R16::{}, result);", upper(opcode.reg_set.index_reg)),
            "flags(new_flags);"
            "pass_time(7);"}};

  if (opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 0)
    return {"ld (bc), a", {"write(get(R16::BC), get(R8::A));"s}};
  if (opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 1)
    return {"ld (de), a", {"write(get(R16::DE), get(R8::A));"s}};
  if (opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 2)
    return {std::format("ld ($nnnn), {}", opcode.reg_set.index_reg),
        {
            "const auto address = read_immediate16();",
            std::format("write(address, get(R8::{}));", upper(opcode.reg_set.index_reg_low)),
            std::format("write(address+1, get(R8::{}));", upper(opcode.reg_set.index_reg_high)),
        }};
  if (opcode.x == 0 && opcode.z == 2 && opcode.q == 0 && opcode.p == 3)
    return {"ld ($nnnn), a", {"write(read_immediate16(), get(R8::A));"}};

  if (opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 0)
    return {"ld a, (bc)", {"set(R8::A, read(get(R16::BC)));"}};
  if (opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 1)
    return {"ld a, (de)", {"set(R8::A, read(get(R16::DE)));"}};
  if (opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 2)
    return {std::format("ld {}, ($nnnn)", opcode.reg_set.index_reg),
        {
            "const auto address = read_immediate16();",
            std::format("set(R8::{}, read(address));", upper(opcode.reg_set.index_reg_low)),
            std::format("set(R8::{}, read(address + 1));", upper(opcode.reg_set.index_reg_high)),
        }};
  if (opcode.x == 0 && opcode.z == 2 && opcode.q == 1 && opcode.p == 3)
    return {"ld ($nnnn), a", {"set(R8::A, read(read_immediate16()));"}};

  if (opcode.x == 0 && opcode.z == 3 && opcode.q == 0)
    return {std::format("inc {}", opcode.reg_set.rp[opcode.p]),
        {std::format(
             "set(R16::{}, get(R16::{}) + 1);", upper(opcode.reg_set.rp[opcode.p]), upper(opcode.reg_set.rp[opcode.p])),
            "pass_time(2);"}};
  if (opcode.x == 0 && opcode.z == 3 && opcode.q == 1)
    return {std::format("dec {}", opcode.reg_set.rp[opcode.p]),
        {std::format(
             "set(R16::{}, get(R16::{}) - 1);", upper(opcode.reg_set.rp[opcode.p]), upper(opcode.reg_set.rp[opcode.p])),
            "pass_time(2);"}};

  if (opcode.x == 0 && (opcode.z == 4 || opcode.z == 5)) {
    const auto operation = opcode.z == 4 ? "inc" : "dec";
    if (opcode.y == 6) {
      return {std::format("{} {}", operation, opcode.reg_set.r[opcode.y]),
          {std::format("const auto address = regs_.wz();"), std::format("const auto rhs = read(address);"),
              "pass_time(1);", std::format("const auto [result, new_flags] = Alu::{}8(rhs, flags());", operation),
              std::format("write(address, result);", upper(opcode.reg_set.r[opcode.y])), "flags(new_flags);"},
          true};
    }
    return {std::format("{} {}", operation, opcode.reg_set.r[opcode.y]),
        {std::format("const auto rhs = get(R8::{});", upper(opcode.reg_set.r[opcode.y])),
            std::format("const auto [result, new_flags] = Alu::{}8(rhs, flags());", operation),
            std::format("set(R8::{}, result);", upper(opcode.reg_set.r[opcode.y])), "flags(new_flags);"}};
  }

  if (opcode.x == 0 && opcode.z == 6) {
    if (opcode.y == 6) {
      return {std::format("ld {}, $nn", opcode.reg_set.r[opcode.y]), {"write(get(R16::HL), read_immediate());"}, true,
          true};
    }
    return {std::format("ld {}, $nn", opcode.reg_set.r[opcode.y]),
        {
            std::format("set(R8::{}, read_immediate());", upper(opcode.reg_set.r[opcode.y])),
        },
        false, true};
  }

  return {"??", {"throw std::runtime_error(\"moo\");"s}};
}

} // namespace

int main(int argc, const char *argv[]) {
  std::ofstream maybe_out;
  if (argc > 1)
    maybe_out.open(argv[1]);
  std::ostream &out = argc > 1 ? maybe_out : std::cout;
  std::print(out, R"(// Automatically generated, DO NOT EDIT

#include "DisassembleInternal.hpp"
#include "z80/common/Alu.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/v3/Z80.hpp"

namespace specbolt::v3 {{

using R16 = RegisterFile::R16;
using R8 = RegisterFile::R8;

void Z80::execute_one_base() {{
    const auto opcode = read_opcode();
    switch (opcode) {{
)");

  for (std::size_t opcode_num = 0; opcode_num < 256; ++opcode_num) {
    const auto opcode = Opcode{opcode_num, base_set};
    const auto op = match_op(opcode);
    std::print(out, "    case 0x{:02x}: {{ // {}\n", opcode_num, op.name);

    if (op.indirect) {
      std::print(out, "      regs_.wz(get(R16::HL));\n");
    }

    for (const auto &line: op.code)
      std::print(out, "      {}\n", line);
    std::print(out, "      break;\n    }}\n", opcode_num);
  }
  std::print(out, "  }}\n}}\n");

  std::print(out, R"(

std::string_view impl::disassemble_base(std::uint8_t opcode) {{
    switch (opcode) {{
)");

  for (std::size_t opcode_num = 0; opcode_num < 256; ++opcode_num) {
    const auto opcode = Opcode{opcode_num, base_set};
    const auto op = match_op(opcode);
    std::print(out, "    case 0x{:02x}: return \"{}\";\n", opcode_num, op.name);
  }
  std::print(out, "  }}\n  return \"??\";\n  }}\n");

  std::print(out, "\n\n}}\n");
}
