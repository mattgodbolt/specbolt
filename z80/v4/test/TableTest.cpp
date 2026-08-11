#include <catch2/catch_test_macros.hpp>

#include "Table.hpp"

#include "z80/v4/Z80.hpp"

#include "peripherals/Memory.hpp"

namespace specbolt::v4 {

TEST_CASE("Table parsing") {
  SECTION("Reads the field vocabulary") {
    STATIC_CHECK(fields.size() == 5);
    STATIC_CHECK(fields[0].name == 'p');
    STATIC_CHECK(fields[0].values.size() == 4);
    STATIC_CHECK(fields[0].values[0].display == "bc");
    STATIC_CHECK(fields[0].values[3].display == "sp");
  }
  SECTION("Reads the instruction rows") {
    STATIC_CHECK(rows.size() == 28);
    STATIC_CHECK(rows[0].mnemonic == "nop");
    STATIC_CHECK(rows[0].steps[0].verb == "nop");
    STATIC_CHECK(rows[0].matched.opcode_bits == 0x00);
  }
  SECTION("Keeps the line number for diagnostics") {
    STATIC_CHECK(rows[0].line == 13);
    STATIC_CHECK(rows[2].line == 15);
  }
  SECTION("A hole means the row does not cover that opcode") {
    STATIC_CHECK(fields[3].name == 'w');
    STATIC_CHECK(fields[3].values[3].hole); // cp has its own rows
    STATIC_CHECK(!fields[3].values[2].hole);
  }
  SECTION("Parentheses make an operand an address") {
    STATIC_CHECK(fields[1].name == 'r');
    STATIC_CHECK(fields[1].values[6].display == "(hl)");
    constexpr auto ld = rows[*find_row(entry_table, 0x46)]; // ld b, (hl)
    STATIC_CHECK(resolve(fields, ld.steps[0].operands[0], ld.matched, 0x46, ld.line).indirect);
    STATIC_CHECK(!resolve(fields, ld.steps[0].destinations[0], ld.matched, 0x46, ld.line).indirect);
    STATIC_CHECK(resolve(fields, ld.steps[0].destinations[0], ld.matched, 0x70, ld.line).indirect); // ld (hl), b
    STATIC_CHECK(find_row(entry_table, 0x86)); // add a, (hl)
    STATIC_CHECK(find_row(entry_table, 0x70)); // ld (hl), b
  }
  SECTION("Members bind to primitives and a carry policy") {
    constexpr auto alu = fields[2];
    STATIC_CHECK(alu.name == 'q');
    STATIC_CHECK(alu.values[0].display == "add");
    STATIC_CHECK(alu.values[0].primitive == "add8");
    STATIC_CHECK(alu.values[0].appended->kind == Operand::Kind::Constant);
    STATIC_CHECK(alu.values[1].display == "adc");
    STATIC_CHECK(alu.values[1].primitive == "add8");
    STATIC_CHECK(alu.values[1].appended->name == Name{"carry"});
    STATIC_CHECK(fields[3].values[3].hole);
    STATIC_CHECK(fields[0].values[0].primitive.empty());
  }
  SECTION("Reports how much of the instruction set it covers") {
    // Only ever goes up. Rows are checked for precedence at compile time, so
    // there is no way to gain coverage by silently shadowing another row.
    STATIC_CHECK(tables.size() == 2);
    STATIC_CHECK(tables[entry_table] == "base");
    STATIC_CHECK(decoded_count == 374);
  }
  SECTION("Finds rows by opcode") {
    STATIC_CHECK(find_row(entry_table, 0x00) == 0u);
    STATIC_CHECK(find_row(entry_table, 0x76) == 1u);
    STATIC_CHECK(rows[*find_row(entry_table, 0x21)].steps[0].verb == "ld16");
    STATIC_CHECK(!find_row(entry_table, 0x08));
  }
  SECTION("Lowers mnemonics into validated pieces") {
    constexpr auto ld = rows[*find_row(entry_table, 0x21)];
    STATIC_CHECK(ld.length == 3);
    STATIC_CHECK(ld.pieces.size() == 4);
    STATIC_CHECK(ld.pieces[0].kind == Piece::Kind::Literal);
    STATIC_CHECK(ld.pieces[0].text == "ld ");
    STATIC_CHECK(ld.pieces[1].kind == Piece::Kind::Field);
    STATIC_CHECK(ld.pieces[2].text == ", ");
    STATIC_CHECK(ld.pieces[3].kind == Piece::Kind::Imm16);
    STATIC_CHECK(rows[*find_row(entry_table, 0x00)].length == 1);
    STATIC_CHECK(rows[*find_row(entry_table, 0x03)].length == 1);
  }
  SECTION("Extracts field values from the opcode") {
    constexpr auto ld = rows[*find_row(entry_table, 0x21)];
    STATIC_CHECK(field_value(ld, 'p', 0x01) == 0);
    STATIC_CHECK(field_value(ld, 'p', 0x21) == 2);
    STATIC_CHECK(field_value(ld, 'p', 0x31) == 3);
  }
}

TEST_CASE("Generated execution") {
  Scheduler scheduler;
  Memory memory{4};
  Z80 cpu{scheduler, memory};
  constexpr std::uint16_t base_address = 0x8000;
  // Assemble one instruction at a fixed address and step the CPU over it.
  const auto run = [&](const auto... bytes) {
    write_to_memory(memory, base_address, static_cast<std::uint8_t>(bytes)...);
    cpu.regs().pc(base_address);
    cpu.execute_one();
  };
  SECTION("ld rr, nn") {
    run(0x21, 0x4000 & 0xff, 0x4000 >> 8);
    CHECK(cpu.get(RegisterFile::R16::HL) == 0x4000);
    run(0x11, 0xbeef & 0xff, 0xbeef >> 8);
    CHECK(cpu.get(RegisterFile::R16::DE) == 0xbeef);
    run(0x31, 0xfffe & 0xff, 0xfffe >> 8);
    CHECK(cpu.get(RegisterFile::R16::SP) == 0xfffe);
  }
  SECTION("inc rr and dec rr") {
    run(0x01, 0x1234 & 0xff, 0x1234 >> 8);
    run(0x03);
    CHECK(cpu.get(RegisterFile::R16::BC) == 0x1235);
    run(0x0b);
    run(0x0b);
    CHECK(cpu.get(RegisterFile::R16::BC) == 0x1233);
  }
  SECTION("inc rr wraps") {
    run(0x21, 0xffff & 0xff, 0xffff >> 8);
    run(0x23);
    CHECK(cpu.get(RegisterFile::R16::HL) == 0);
  }
  SECTION("nop does nothing, halt halts") {
    run(0x21, 0x1234 & 0xff, 0x1234 >> 8);
    run(0x00);
    CHECK(cpu.get(RegisterFile::R16::HL) == 0x1234);
    CHECK(!cpu.halted());
    run(0x76);
    CHECK(cpu.halted());
  }
  SECTION("add ignores the carry flag, adc reads it") {
    cpu.set(RegisterFile::R8::A, 0x10);
    cpu.set(RegisterFile::R8::F, Flags::Carry().to_u8());
    run(0xc6, 0x01);
    CHECK(cpu.get(RegisterFile::R8::A) == 0x11);

    cpu.set(RegisterFile::R8::A, 0x10);
    cpu.set(RegisterFile::R8::F, Flags::Carry().to_u8());
    run(0xce, 0x01);
    CHECK(cpu.get(RegisterFile::R8::A) == 0x12);
  }
  SECTION("sub and sbc likewise") {
    cpu.set(RegisterFile::R8::A, 0x10);
    cpu.set(RegisterFile::R8::F, Flags::Carry().to_u8());
    run(0xd6, 0x01);
    CHECK(cpu.get(RegisterFile::R8::A) == 0x0f);

    cpu.set(RegisterFile::R8::A, 0x10);
    cpu.set(RegisterFile::R8::F, Flags::Carry().to_u8());
    run(0xde, 0x01);
    CHECK(cpu.get(RegisterFile::R8::A) == 0x0e);
  }
  SECTION("logic operations take no carry input") {
    cpu.set(RegisterFile::R8::A, 0xf0);
    cpu.set(RegisterFile::R8::F, Flags::Carry().to_u8());
    run(0xe6, 0x3f);
    CHECK(cpu.get(RegisterFile::R8::A) == 0x30);
    run(0xee, 0xff);
    CHECK(cpu.get(RegisterFile::R8::A) == 0xcf);
    run(0xf6, 0x0f);
    CHECK(cpu.get(RegisterFile::R8::A) == 0xcf);
  }
  SECTION("cp leaves a alone but sets flags") {
    cpu.set(RegisterFile::R8::A, 0x42);
    run(0xfe, 0x42);
    CHECK(cpu.get(RegisterFile::R8::A) == 0x42);
    CHECK(Flags(cpu.get(RegisterFile::R8::F)).zero());
    run(0xfe, 0x43);
    CHECK(cpu.get(RegisterFile::R8::A) == 0x42);
    CHECK(Flags(cpu.get(RegisterFile::R8::F)).carry());
  }
  SECTION("Accumulator operations") {
    cpu.set(RegisterFile::R8::A, 0x0f);
    run(0x2f); // cpl
    CHECK(cpu.get(RegisterFile::R8::A) == 0xf0);

    cpu.set(RegisterFile::R8::F, 0);
    run(0x37); // scf
    CHECK(Flags(cpu.get(RegisterFile::R8::F)).carry());
    run(0x3f); // ccf
    CHECK(!Flags(cpu.get(RegisterFile::R8::F)).carry());
    CHECK(cpu.get(RegisterFile::R8::A) == 0xf0);
  }
  SECTION("ld r, r'") {
    cpu.set(RegisterFile::R8::C, 0x37);
    run(0x41); // ld b, c
    CHECK(cpu.get(RegisterFile::R8::B) == 0x37);
    run(0x7f); // ld a, a
    CHECK(cpu.get(RegisterFile::R8::C) == 0x37);
  }
  SECTION("inc r and dec r") {
    cpu.set(RegisterFile::R8::B, 0x7f);
    run(0x04); // inc b
    CHECK(cpu.get(RegisterFile::R8::B) == 0x80);
    CHECK(Flags(cpu.get(RegisterFile::R8::F)).overflow());
    run(0x05); // dec b
    CHECK(cpu.get(RegisterFile::R8::B) == 0x7f);
  }
  SECTION("daa reads and writes the flags") {
    cpu.set(RegisterFile::R8::A, 0x0f);
    cpu.set(RegisterFile::R8::F, 0);
    run(0x27);
    CHECK(cpu.get(RegisterFile::R8::A) == 0x15);
  }
  SECTION("Undecoded opcodes are rejected, not ignored") { CHECK_THROWS(run(0x08)); }
  SECTION("Operands can be addresses") {
    cpu.set(RegisterFile::R16::HL, 0x9000);
    cpu.set(RegisterFile::R8::B, 0x5a);
    run(0x70); // ld (hl), b
    CHECK(memory.read(0x9000) == 0x5a);
    run(0x4e); // ld c, (hl)
    CHECK(cpu.get(RegisterFile::R8::C) == 0x5a);
    cpu.set(RegisterFile::R8::A, 0x01);
    run(0x86); // add a, (hl)
    CHECK(cpu.get(RegisterFile::R8::A) == 0x5b);
    run(0x36, 0x99); // ld (hl), n
    CHECK(memory.read(0x9000) == 0x99);
  }
  SECTION("The address can come from any register pair") {
    cpu.set(RegisterFile::R16::DE, 0x9010);
    cpu.set(RegisterFile::R8::A, 0x3c);
    run(0x12); // ld (de), a
    CHECK(memory.read(0x9010) == 0x3c);
    cpu.set(RegisterFile::R16::BC, 0x9010);
    cpu.set(RegisterFile::R8::A, 0);
    run(0x0a); // ld a, (bc)
    CHECK(cpu.get(RegisterFile::R8::A) == 0x3c);
  }
  SECTION("A prefix transfers to another table") {
    cpu.set(RegisterFile::R8::B, 0b0000'0000);
    run(0xcb, 0xc0); // set 0, b
    CHECK(cpu.get(RegisterFile::R8::B) == 0b0000'0001);
    run(0xcb, 0xf8); // set 7, b
    CHECK(cpu.get(RegisterFile::R8::B) == 0b1000'0001);
    run(0xcb, 0x80); // res 0, b
    CHECK(cpu.get(RegisterFile::R8::B) == 0b1000'0000);
    run(0xcb, 0x78); // bit 7, b
    CHECK(!Flags(cpu.get(RegisterFile::R8::F)).zero());
    run(0xcb, 0x40); // bit 0, b
    CHECK(Flags(cpu.get(RegisterFile::R8::F)).zero());
  }
  SECTION("A prefixed instruction can reach memory") {
    cpu.set(RegisterFile::R16::HL, 0x9000);
    run(0xcb, 0xfe); // set 7, (hl)
    CHECK(memory.read(0x9000) == 0x80);
    run(0xcb, 0xbe); // res 7, (hl)
    CHECK(memory.read(0x9000) == 0x00);
  }
  SECTION("Timing falls out of the fetch cycle") {
    const auto cycles = [&](const auto... bytes) {
      const auto before = cpu.cycle_count();
      run(bytes...);
      return cpu.cycle_count() - before;
    };
    CHECK(cycles(0x00) == 4); // nop
    CHECK(cycles(0x01, 0x00, 0x00) == 10); // ld bc, nn
    CHECK(cycles(0x80) == 4); // add a, b
    CHECK(cycles(0xc6, 0x01) == 7); // add a, n
    CHECK(cycles(0x4e) == 7); // ld c, (hl)
    CHECK(cycles(0x70) == 7); // ld (hl), b
    CHECK(cycles(0x36, 0x00) == 10); // ld (hl), n
    CHECK(cycles(0x03) == 6); // inc bc: two internal cycles
    CHECK(cycles(0x34) == 11); // inc (hl): read, modify, write, plus one
    CHECK(cycles(0xcb, 0xc0) == 8); // set 0, b: two opcode fetches
    CHECK(cycles(0xcb, 0xfe) == 15); // set 7, (hl)
    CHECK(cycles(0xcb, 0x7e) == 12); // bit 7, (hl)
  }
}

} // namespace specbolt::v4
