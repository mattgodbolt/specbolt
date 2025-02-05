#include "z80/RegisterFile.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using specbolt::RegisterFile;

TEST_CASE("RegisterFile tests") {
  RegisterFile rf;
  SECTION("starts out zero") {
    SECTION("8 bit") {
      const auto r8 = GENERATE(RegisterFile::R8::A, RegisterFile::R8::F, RegisterFile::R8::B, RegisterFile::R8::B_);
      CHECK(rf.get(r8) == 0);
    }
    SECTION("16 bit") {
      const auto r8 = GENERATE(RegisterFile::R16::AF, RegisterFile::R16::AF_, RegisterFile::R16::HL);
      CHECK(rf.get(r8) == 0);
    }
    SECTION("special") {
      CHECK(rf.ix() == 0);
      CHECK(rf.iy() == 0);
      CHECK(rf.pc() == 0);
      CHECK(rf.sp() == 0);
      CHECK(rf.r() == 0);
      CHECK(rf.i() == 0);
    }
  }
  SECTION("Can set and get register pairs correctly") {
    // TODO look up how to do this properly
    // const auto [highlow, high, low] = GENERATE({RegisterFile::R16::AF, RegisterFile::R8::A, RegisterFile::R8::F});
    const auto highlow = RegisterFile::R16::AF;
    const auto high = RegisterFile::R8::A;
    const auto low = RegisterFile::R8::F;
    rf.set(highlow, 0x1234);
    CHECK(rf.get(highlow) == 0x1234);
    CHECK(rf.get(high) == 0x12);
    CHECK(rf.get(low) == 0x34);
    rf.set(high, 0x00);
    CHECK(rf.get(high) == 0x00);
    CHECK(rf.get(highlow) == 0x0034);
    rf.set(low, 0xff);
    CHECK(rf.get(low) == 0xff);
    CHECK(rf.get(highlow) == 0x00ff);
  }
  SECTION("exxes") {
    rf.set(RegisterFile::R16::BC, 0x1234);
    rf.set(RegisterFile::R16::DE, 0x2345);
    rf.set(RegisterFile::R16::HL, 0x3456);
    rf.set(RegisterFile::R16::BC_, 0x4567);
    rf.set(RegisterFile::R16::DE_, 0x5678);
    rf.set(RegisterFile::R16::HL_, 0x6789);
    rf.exx();
    CHECK(rf.get(RegisterFile::R16::BC) == 0x4567);
    CHECK(rf.get(RegisterFile::R16::DE) == 0x5678);
    CHECK(rf.get(RegisterFile::R16::HL) == 0x6789);
    CHECK(rf.get(RegisterFile::R16::BC_) == 0x1234);
    CHECK(rf.get(RegisterFile::R16::DE_) == 0x2345);
    CHECK(rf.get(RegisterFile::R16::HL_) == 0x3456);
  }
}
