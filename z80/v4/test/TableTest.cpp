#include <catch2/catch_test_macros.hpp>

#include "Execute.hpp"
#include "Table.hpp"

namespace specbolt::v4 {

TEST_CASE("Table parsing") {
  SECTION("Reads the field vocabulary") {
    STATIC_CHECK(fields.size() == 1);
    STATIC_CHECK(fields[0].name == 'p');
    STATIC_CHECK(fields[0].num_values == 4);
    STATIC_CHECK(fields[0].values[0] == "bc");
    STATIC_CHECK(fields[0].values[3] == "sp");
  }
  SECTION("Reads the instruction rows") {
    STATIC_CHECK(rows.size() == 5);
    STATIC_CHECK(rows[0].mnemonic == "nop");
    STATIC_CHECK(rows[0].verb == "nop");
    STATIC_CHECK(rows[0].matched.opcode_bits == 0x00);
  }
  SECTION("Keeps the line number for diagnostics") {
    STATIC_CHECK(rows[0].line == 7);
    STATIC_CHECK(rows[2].line == 9);
  }
  SECTION("Finds rows by opcode") {
    STATIC_CHECK(find_row(0x00) == 0u);
    STATIC_CHECK(find_row(0x76) == 1u);
    STATIC_CHECK(rows[*find_row(0x21)].verb == "ld16");
    STATIC_CHECK(!find_row(0x08));
  }
  SECTION("Lowers mnemonics into validated pieces") {
    constexpr auto ld = rows[*find_row(0x21)];
    STATIC_CHECK(ld.length == 3);
    STATIC_CHECK(ld.num_pieces == 4);
    STATIC_CHECK(ld.pieces[0].kind == Piece::Kind::Literal);
    STATIC_CHECK(ld.pieces[0].text == "ld ");
    STATIC_CHECK(ld.pieces[1].kind == Piece::Kind::Field);
    STATIC_CHECK(ld.pieces[2].text == ", ");
    STATIC_CHECK(ld.pieces[3].kind == Piece::Kind::Imm16);
    STATIC_CHECK(rows[*find_row(0x00)].length == 1);
    STATIC_CHECK(rows[*find_row(0x03)].length == 1);
  }
  SECTION("Extracts field values from the opcode") {
    constexpr auto ld = rows[*find_row(0x21)];
    STATIC_CHECK(field_value(ld, 'p', 0x01) == 0);
    STATIC_CHECK(field_value(ld, 'p', 0x21) == 2);
    STATIC_CHECK(field_value(ld, 'p', 0x31) == 3);
  }
}

TEST_CASE("Generated execution") {
  Cpu cpu;
  SECTION("ld rr, nn") {
    execute(cpu, 0x21, 0x4000);
    CHECK(cpu.registers.get(RegisterFile::R16::HL) == 0x4000);
    execute(cpu, 0x11, 0xbeef);
    CHECK(cpu.registers.get(RegisterFile::R16::DE) == 0xbeef);
    execute(cpu, 0x31, 0xfffe);
    CHECK(cpu.registers.get(RegisterFile::R16::SP) == 0xfffe);
  }
  SECTION("inc rr and dec rr") {
    execute(cpu, 0x01, 0x1234);
    execute(cpu, 0x03);
    CHECK(cpu.registers.get(RegisterFile::R16::BC) == 0x1235);
    execute(cpu, 0x0b);
    execute(cpu, 0x0b);
    CHECK(cpu.registers.get(RegisterFile::R16::BC) == 0x1233);
  }
  SECTION("inc rr wraps") {
    execute(cpu, 0x21, 0xffff);
    execute(cpu, 0x23);
    CHECK(cpu.registers.get(RegisterFile::R16::HL) == 0);
  }
  SECTION("nop does nothing, halt halts") {
    execute(cpu, 0x21, 0x1234);
    execute(cpu, 0x00);
    CHECK(cpu.registers.get(RegisterFile::R16::HL) == 0x1234);
    CHECK(!cpu.halted);
    execute(cpu, 0x76);
    CHECK(cpu.halted);
  }
  SECTION("Unknown opcodes are inert") {
    execute(cpu, 0x21, 0x1234);
    execute(cpu, 0x08);
    CHECK(cpu.registers.get(RegisterFile::R16::HL) == 0x1234);
  }
}

} // namespace specbolt::v4
