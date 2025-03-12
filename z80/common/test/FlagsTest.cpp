#include "z80/common/Flags.hpp"

#include <catch2/catch_test_macros.hpp>

namespace specbolt {

TEST_CASE("Flags tests") {
  SECTION("starts empty") { CHECK(Flags().to_u8() == 0); }
  SECTION("uses correct bits for flags") {
    CHECK(Flags::Sign().to_u8() == 0x80);
    CHECK(Flags::Zero().to_u8() == 0x40);
    CHECK(Flags::Flag5().to_u8() == 0x20);
    CHECK(Flags::HalfCarry().to_u8() == 0x10);
    CHECK(Flags::Flag3().to_u8() == 0x08);
    CHECK(Flags::Parity().to_u8() == 0x04);
    CHECK(Flags::Overflow().to_u8() == 0x04);
    CHECK(Flags::Subtract().to_u8() == 0x02);
    CHECK(Flags::Carry().to_u8() == 0x01);
  }
  SECTION("ors") { CHECK((Flags::Sign() | Flags::HalfCarry()).to_u8() == 0x90); }
  SECTION("ands") {
    CHECK((Flags::Sign() & Flags::HalfCarry()) == Flags());
    CHECK((Flags::Sign() & Flags::Sign()) == Flags::Sign());
  }
  SECTION("stringification") {
    CHECK(Flags().to_string() == "sz_h_pnc (0x00)");
    CHECK(Flags(0xff).to_string() == "SZ5H3PNC (0xff)");
    CHECK(Flags::Carry().to_string() == "sz_h_pnC (0x01)");
    CHECK(Flags::Subtract().to_string() == "sz_h_pNc (0x02)");
    CHECK(Flags::Parity().to_string() == "sz_h_Pnc (0x04)");
    CHECK(Flags::Overflow().to_string() == "sz_h_Pnc (0x04)");
    CHECK(Flags::Flag3().to_string() == "sz_h3pnc (0x08)");
    CHECK(Flags::HalfCarry().to_string() == "sz_H_pnc (0x10)");
    CHECK(Flags::Flag5().to_string() == "sz5h_pnc (0x20)");
    CHECK(Flags::Zero().to_string() == "sZ_h_pnc (0x40)");
    CHECK(Flags::Sign().to_string() == "Sz_h_pnc (0x80)");
  }
}

} // namespace specbolt
