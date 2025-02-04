#include "../Alu.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <format>

template<>
struct Catch::StringMaker<specbolt::Alu::Result> {
  static std::string convert(const specbolt::Alu::Result &value) {
    return std::format("{:02x} ({})", value.result, value.flags.to_string());
  }
};


namespace specbolt {

TEST_CASE("ALU tests") {
  SECTION("8 bit addition") {
    SECTION("no carry in") {
      CHECK(Alu::add8(0, 0, false) == Alu::Result{0, Flags::Zero()});
      CHECK(Alu::add8(0, 1, false) == Alu::Result{1, Flags()});
      CHECK(Alu::add8(1, 0, false) == Alu::Result{1, Flags()});
      CHECK(Alu::add8(0x7f, 0x01, false) == Alu::Result{0x80, Flags::Sign() | Flags::HalfCarry() | Flags::Overflow()});
      CHECK(Alu::add8(0xff, 0x01, false) == Alu::Result{0x00, Flags::Carry() | Flags::Zero() | Flags::HalfCarry()});
      CHECK(Alu::add8(0xff, 0x00, false) == Alu::Result{0xff, Flags::Flag3() | Flags::Flag5() | Flags::Sign()});
    }
    SECTION("with carry in") {
      CHECK(Alu::add8(0, 0, true) == Alu::Result{1, Flags()});
      CHECK(Alu::add8(0xff, 0x00, true) == Alu::Result{0x00, Flags::Carry() | Flags::Zero() | Flags::HalfCarry()});
      CHECK(Alu::add8(0xff, 0xff, true) ==
            Alu::Result{0xff, Flags::Carry() | Flags::HalfCarry() | Flags::Flag5() | Flags::Flag3() | Flags::Sign()});
    }
    SECTION("half carry") {
      CHECK(!Alu::add8(0x0f, 0x00, false).flags.half_carry());
      CHECK(!Alu::add8(0x03, 0x0a, false).flags.half_carry());
      CHECK(Alu::add8(0x0f, 0x01, false).flags.half_carry());
      CHECK(Alu::add8(0x01, 0x0f, false).flags.half_carry());
      CHECK(Alu::add8(0x08, 0x08, false).flags.half_carry());
      CHECK(Alu::add8(0x0f, 0x0f, false).flags.half_carry());
    }
    SECTION("overflow") {
      CHECK(!Alu::add8(0xff, 0x00, false).flags.overflow());
      CHECK(!Alu::add8(0x7f, 0x00, false).flags.overflow());
      CHECK(!Alu::add8(0x00, 0x7f, false).flags.overflow());
      CHECK(Alu::add8(0x01, 0x7f, false).flags.overflow());
      CHECK(Alu::add8(0x7f, 0x01, false).flags.overflow());
      CHECK(Alu::add8(0xff, 0x80, false).flags.overflow());
    }
  }
  SECTION("Exclusive or") {
    CHECK(Alu::xor8(0, 0) == Alu::Result{0, Flags::Zero() | Flags::Parity()});
    CHECK(Alu::xor8(0, 0xff) == Alu::Result{0xff, Flags::Sign() | Flags::Flag5() | Flags::Flag3() | Flags::Parity()});
    CHECK(Alu::xor8(1, 0xff) == Alu::Result{0xfe, Flags::Sign() | Flags::Flag5() | Flags::Flag3()});
  }
}

} // namespace specbolt
