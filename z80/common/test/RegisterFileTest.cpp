#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

import z80_common;

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
    struct RegisterPair {
      RegisterFile::R16 highlow;
      RegisterFile::R8 high;
      RegisterFile::R8 low;
    };
    const auto [highlow, high, low] =
        GENERATE(RegisterPair{RegisterFile::R16::AF, RegisterFile::R8::A, RegisterFile::R8::F},
            RegisterPair{RegisterFile::R16::BC, RegisterFile::R8::B, RegisterFile::R8::C},
            RegisterPair{RegisterFile::R16::DE, RegisterFile::R8::D, RegisterFile::R8::E},
            RegisterPair{RegisterFile::R16::HL, RegisterFile::R8::H, RegisterFile::R8::L},
            RegisterPair{RegisterFile::R16::IX, RegisterFile::R8::IXH, RegisterFile::R8::IXL},
            RegisterPair{RegisterFile::R16::IY, RegisterFile::R8::IYH, RegisterFile::R8::IYL});
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
