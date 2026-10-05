#include <catch2/catch_test_macros.hpp>

#include "Target.hpp"
#include "refract/Execute.hpp"

namespace specbolt::v4 {

using C = Target::Compiled;

using namespace refract;

TEST_CASE("Table parsing") {
  SECTION("Reads the field vocabulary") {
    STATIC_CHECK(C::vocabularies()[0].name == "pair");
    STATIC_CHECK(C::vocabularies()[0].members.size() == 4);
    STATIC_CHECK(C::vocabularies()[0].members[0].display == "bc");
    STATIC_CHECK(C::vocabularies()[0].members[3].display == "sp");
  }
  SECTION("Reads the instruction rows") {
    STATIC_CHECK(C::rows()[0].mnemonic == "nop");
    STATIC_CHECK(refract::steps_of(C::rows()[0])[0].operation == "nop");
    STATIC_CHECK(C::rows()[0].matched.opcode_bits == 0x00);
  }
  SECTION("A hole means the row does not cover that opcode") {
    STATIC_CHECK(C::vocabularies()[3].name == "logic");
    STATIC_CHECK(std::holds_alternative<Member::Hole>(C::vocabularies()[3].members[3].kind)); // cp has its own rows
    STATIC_CHECK(!std::holds_alternative<Member::Hole>(C::vocabularies()[3].members[2].kind));
  }
  SECTION("Parentheses make an operand an address") {
    STATIC_CHECK(C::vocabularies()[1].name == "reg");
    STATIC_CHECK(C::vocabularies()[1].members[6].display == "(hl)");
    constexpr auto ld = C::rows()[*C::decoded()[C::entry_table][0x46]]; // ld b, (hl)
    STATIC_CHECK(resolve({.vocabularies = C::vocabularies(), .matched = ld.matched, .opcode = 0x46}, //
        refract::steps_of(ld)[0].operands[0])
            .indirect);
    STATIC_CHECK(!resolve({.vocabularies = C::vocabularies(), .matched = ld.matched, .opcode = 0x46}, //
        refract::steps_of(ld)[0].destinations[0])
            .indirect);
    // ld (hl), b
    STATIC_CHECK(resolve({.vocabularies = C::vocabularies(), .matched = ld.matched, .opcode = 0x70}, //
        refract::steps_of(ld)[0].destinations[0])
            .indirect);
    STATIC_CHECK(C::decoded()[C::entry_table][0x86]); // add a, (hl)
    STATIC_CHECK(C::decoded()[C::entry_table][0x70]); // ld (hl), b
  }
  SECTION("Members bind to operations and a carry policy") {
    constexpr auto alu = C::vocabularies()[2];
    STATIC_CHECK(alu.name == "arith");
    STATIC_CHECK(alu.members[0].display == "add");
    constexpr auto add = std::get<Member::Operation>(alu.members[0].kind);
    STATIC_CHECK(add.name == "add8");
    STATIC_CHECK(add.arguments[0].kind == Operand::Kind{Operand::Constant{0}});
    STATIC_CHECK(alu.members[1].display == "adc");
    constexpr auto adc = std::get<Member::Operation>(alu.members[1].kind);
    STATIC_CHECK(adc.name == "add8");
    STATIC_CHECK(adc.arguments[0].kind == Operand::Kind{Operand::Named{"carry"}});
    STATIC_CHECK(std::holds_alternative<Operand>(C::vocabularies()[0].members[0].kind));
  }
  SECTION("Decoding starts in the first table declared") { STATIC_CHECK(C::tables()[C::entry_table].name == "base"); }
  SECTION("Finds rows by opcode") {
    STATIC_CHECK(C::decoded()[C::entry_table][0x00] == 0u);
    STATIC_CHECK(C::decoded()[C::entry_table][0x76] == 1u);
    STATIC_CHECK(refract::steps_of(C::rows()[*C::decoded()[C::entry_table][0x21]])[0].operation == "ld16");
  }
  SECTION("Lowers mnemonics into validated pieces") {
    constexpr auto ld = C::rows()[*C::decoded()[C::entry_table][0x21]];
    STATIC_CHECK(ld.immediate_bytes == 2);
    STATIC_CHECK(ld.pieces.size() == 4);
    STATIC_CHECK(ld.pieces[0] == Piece{Piece::Literal{"ld "}});
    STATIC_CHECK(std::holds_alternative<Piece::Vocabulary>(ld.pieces[1].kind));
    STATIC_CHECK(ld.pieces[2] == Piece{Piece::Literal{", "}});
    STATIC_CHECK(ld.pieces[3] == Piece{Piece::Imm16{}});
    STATIC_CHECK(C::rows()[*C::decoded()[C::entry_table][0x00]].immediate_bytes == 0);
  }
  SECTION("Extracts field values from the opcode") {
    constexpr auto ld = C::rows()[*C::decoded()[C::entry_table][0x21]];
    constexpr auto slice = ld.matched.slices[*find_slice(ld.matched, 'p')];
    STATIC_CHECK(slice.extract(0x01) == 0);
    STATIC_CHECK(slice.extract(0x21) == 2);
    STATIC_CHECK(slice.extract(0x31) == 3);
  }
}

TEST_CASE("Two opcodes share a body only when every step agrees") {
  // `body_key` decides which opcodes share a generated function, and `resolve` decides what that function does; the
  // slices the first ignores must be the ones the second folds away. refract's own 6502 test checks the same.
  const auto disagreements = refract::Interpreter<Target>::disagreements();
  for (const auto &[table, opcode, line]: disagreements)
    UNSCOPED_INFO("table " << int{table} << " opcode " << int{opcode} << " line " << line);
  CHECK(disagreements.empty());
}

} // namespace specbolt::v4
