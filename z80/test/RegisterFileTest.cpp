#include "z80/RegisterFile.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using specbolt::RegisterFile;

TEST_CASE("RegisterFile tests") {
  RegisterFile rf;
  SECTION("starts out as appropriate") {
    // Apparently according to "The Undocumented Z80 Documented".
    SECTION("8 bit") {
      const auto r8 = GENERATE(RegisterFile::R8::A, RegisterFile::R8::F, RegisterFile::R8::B, RegisterFile::R8::B_);
      CHECK(rf.get(r8) == 0xff);
    }
    SECTION("16 bit") {
      const auto r8 = GENERATE(RegisterFile::R16::AF, RegisterFile::R16::AF_, RegisterFile::R16::HL);
      CHECK(rf.get(r8) == 0xffff);
    }
    SECTION("special") {
      CHECK(rf.ix() == 0xffff);
      CHECK(rf.iy() == 0xffff);
      CHECK(rf.pc() == 0x0000);
      CHECK(rf.sp() == 0xffff);
      CHECK(rf.r() == 0x00);
      CHECK(rf.i() == 0x00);
    }
  }
  SECTION("Can set and get register pairs correctly") {
    // TODO look up how to do this properly
    // const auto [highlow, high, low] = GENERATE({RegisterFile::R16::AF, RegisterFile::R8::A, RegisterFile::R8::F});
    constexpr auto highlow = RegisterFile::R16::AF;
    constexpr auto high = RegisterFile::R8::A;
    constexpr auto low = RegisterFile::R8::F;
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
  SECTION("IX and IY") {
    rf.ix(0x1234);
    rf.iy(0x2345);
    CHECK(rf.ix() == 0x1234);
    CHECK(rf.ixh() == 0x12);
    CHECK(rf.ixl() == 0x34);
    CHECK(rf.iy() == 0x2345);
    CHECK(rf.iyh() == 0x23);
    CHECK(rf.iyl() == 0x45);
    rf.ixh(0xff);
    CHECK(rf.ix() == 0xff34);
    rf.iyh(0xee);
    CHECK(rf.iy() == 0xee45);
    rf.ixl(0xaa);
    CHECK(rf.ix() == 0xffaa);
    rf.iyl(0x99);
    CHECK(rf.iy() == 0xee99);
  }
  SECTION("exchanges") {
    rf.set(RegisterFile::R16::BC, 0x1234);
    rf.set(RegisterFile::R16::DE, 0x2345);
    rf.set(RegisterFile::R16::HL, 0x3456);
    rf.set(RegisterFile::R16::BC_, 0x4567);
    rf.set(RegisterFile::R16::DE_, 0x5678);
    rf.set(RegisterFile::R16::HL_, 0x6789);
    SECTION("exx") {
      rf.exx();
      CHECK(rf.get(RegisterFile::R16::BC) == 0x4567);
      CHECK(rf.get(RegisterFile::R16::DE) == 0x5678);
      CHECK(rf.get(RegisterFile::R16::HL) == 0x6789);
      CHECK(rf.get(RegisterFile::R16::BC_) == 0x1234);
      CHECK(rf.get(RegisterFile::R16::DE_) == 0x2345);
      CHECK(rf.get(RegisterFile::R16::HL_) == 0x3456);
    }
    SECTION("ex de, hl") {
      rf.ex(RegisterFile::R16::DE, RegisterFile::R16::HL);
      CHECK(rf.get(RegisterFile::R16::BC) == 0x1234);
      CHECK(rf.get(RegisterFile::R16::DE) == 0x3456);
      CHECK(rf.get(RegisterFile::R16::HL) == 0x2345);
    }
  }
}
