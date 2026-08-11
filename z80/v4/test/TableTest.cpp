#include <catch2/catch_test_macros.hpp>

#include "Execute.hpp"
#include "Table.hpp"

namespace specbolt::v4 {

TEST_CASE("Table parsing") {
  SECTION("Reads the field vocabulary") {
    STATIC_CHECK(fields.size() == 3);
    STATIC_CHECK(fields[0].name == 'p');
    STATIC_CHECK(fields[0].num_values == 4);
    STATIC_CHECK(fields[0].values[0].display == "bc");
    STATIC_CHECK(fields[0].values[3].display == "sp");
  }
  SECTION("Reads the instruction rows") {
    STATIC_CHECK(rows.size() == 11);
    STATIC_CHECK(rows[0].mnemonic == "nop");
    STATIC_CHECK(rows[0].verb == "nop");
    STATIC_CHECK(rows[0].matched.opcode_bits == 0x00);
  }
  SECTION("Keeps the line number for diagnostics") {
    STATIC_CHECK(rows[0].line == 9);
    STATIC_CHECK(rows[2].line == 11);
  }
  SECTION("Members bind to primitives and a carry policy") {
    constexpr auto alu = fields[1];
    STATIC_CHECK(alu.name == 'q');
    STATIC_CHECK(alu.values[0].display == "add");
    STATIC_CHECK(alu.values[0].primitive == "add8");
    STATIC_CHECK(alu.values[0].carry == CarrySource::Zero);
    STATIC_CHECK(alu.values[1].display == "adc");
    STATIC_CHECK(alu.values[1].primitive == "add8");
    STATIC_CHECK(alu.values[1].carry == CarrySource::FromFlags);
    STATIC_CHECK(fields[2].values[3].display == "cp");
    STATIC_CHECK(fields[2].values[3].primitive == "cmp8");
    STATIC_CHECK(fields[0].values[0].primitive.empty());
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
  SECTION("add ignores the carry flag, adc reads it") {
    cpu.registers.set(RegisterFile::R8::A, 0x10);
    cpu.registers.set(RegisterFile::R8::F, Flags::Carry().to_u8());
    execute(cpu, 0xc6, 0x01);
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0x11);

    cpu.registers.set(RegisterFile::R8::A, 0x10);
    cpu.registers.set(RegisterFile::R8::F, Flags::Carry().to_u8());
    execute(cpu, 0xce, 0x01);
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0x12);
  }
  SECTION("sub and sbc likewise") {
    cpu.registers.set(RegisterFile::R8::A, 0x10);
    cpu.registers.set(RegisterFile::R8::F, Flags::Carry().to_u8());
    execute(cpu, 0xd6, 0x01);
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0x0f);

    cpu.registers.set(RegisterFile::R8::A, 0x10);
    cpu.registers.set(RegisterFile::R8::F, Flags::Carry().to_u8());
    execute(cpu, 0xde, 0x01);
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0x0e);
  }
  SECTION("logic operations take no carry input") {
    cpu.registers.set(RegisterFile::R8::A, 0xf0);
    cpu.registers.set(RegisterFile::R8::F, Flags::Carry().to_u8());
    execute(cpu, 0xe6, 0x3f);
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0x30);
    execute(cpu, 0xee, 0xff);
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0xcf);
    execute(cpu, 0xf6, 0x0f);
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0xcf);
  }
  SECTION("cp leaves a alone but sets flags") {
    cpu.registers.set(RegisterFile::R8::A, 0x42);
    execute(cpu, 0xfe, 0x42);
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0x42);
    CHECK(Flags(cpu.registers.get(RegisterFile::R8::F)).zero());
    execute(cpu, 0xfe, 0x43);
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0x42);
    CHECK(Flags(cpu.registers.get(RegisterFile::R8::F)).carry());
  }
  SECTION("Accumulator operations") {
    cpu.registers.set(RegisterFile::R8::A, 0x0f);
    execute(cpu, 0x2f); // cpl
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0xf0);

    cpu.registers.set(RegisterFile::R8::F, 0);
    execute(cpu, 0x37); // scf
    CHECK(Flags(cpu.registers.get(RegisterFile::R8::F)).carry());
    execute(cpu, 0x3f); // ccf
    CHECK(!Flags(cpu.registers.get(RegisterFile::R8::F)).carry());
    CHECK(cpu.registers.get(RegisterFile::R8::A) == 0xf0);
  }
  SECTION("Unknown opcodes are inert") {
    execute(cpu, 0x21, 0x1234);
    execute(cpu, 0x08);
    CHECK(cpu.registers.get(RegisterFile::R16::HL) == 0x1234);
  }
}

} // namespace specbolt::v4
