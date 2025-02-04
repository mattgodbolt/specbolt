#include "../Alu.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <format>

template<>
struct Catch::StringMaker<specbolt::Alu::R8> {
  static std::string convert(const specbolt::Alu::R8 &value) {
    return std::format("{:02x} ({})", value.result, value.flags.to_string());
  }
};

template<>
struct Catch::StringMaker<specbolt::Alu::R16> {
  static std::string convert(const specbolt::Alu::R16 &value) {
    return std::format("{:04x} ({})", value.result, value.flags.to_string());
  }
};


namespace specbolt {

TEST_CASE("ALU tests") {
  SECTION("8 bit addition") {
    SECTION("no carry in") {
      CHECK(Alu::add8(0, 0, false) == Alu::R8{0, Flags::Zero()});
      CHECK(Alu::add8(0, 1, false) == Alu::R8{1, Flags()});
      CHECK(Alu::add8(1, 0, false) == Alu::R8{1, Flags()});
      CHECK(Alu::add8(0x7f, 0x01, false) == Alu::R8{0x80, Flags::Sign() | Flags::HalfCarry() | Flags::Overflow()});
      CHECK(Alu::add8(0xff, 0x01, false) == Alu::R8{0x00, Flags::Carry() | Flags::Zero() | Flags::HalfCarry()});
      CHECK(Alu::add8(0xff, 0x00, false) == Alu::R8{0xff, Flags::Flag3() | Flags::Flag5() | Flags::Sign()});
    }
    SECTION("with carry in") {
      CHECK(Alu::add8(0, 0, true) == Alu::R8{1, Flags()});
      CHECK(Alu::add8(0xff, 0x00, true) == Alu::R8{0x00, Flags::Carry() | Flags::Zero() | Flags::HalfCarry()});
      CHECK(Alu::add8(0xff, 0xff, true) ==
            Alu::R8{0xff, Flags::Carry() | Flags::HalfCarry() | Flags::Flag5() | Flags::Flag3() | Flags::Sign()});
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
  SECTION("16 bit addition") {
    SECTION("no carry in") {
      CHECK(Alu::add16(0, 0, false) == Alu::R16{0, Flags::Zero()});
      CHECK(Alu::add16(0, 0xff, false) == Alu::R16{0xff, Flags()});
      CHECK(Alu::add16(1, 0xff, false) == Alu::R16{0x100, Flags()});
      CHECK(Alu::add16(0xff00, 0xff, false) == Alu::R16{0xffff, Flags::Sign() | Flags::Flag5() | Flags::Flag3()});
      CHECK(Alu::add16(0xffff, 0, false) == Alu::R16{0xffff, Flags::Sign() | Flags::Flag5() | Flags::Flag3()});
      CHECK(Alu::add16(0xffff, 1, false) == Alu::R16{0, Flags::Zero() | Flags::Carry()});
    }
    SECTION("with carry in") {
      CHECK(Alu::add16(0, 0, true) == Alu::R16{1, Flags()});
      CHECK(Alu::add16(0xff, 0, true) == Alu::R16{0x100, Flags()});
      CHECK(Alu::add16(0xffff, 0, true) == Alu::R16{0x0, Flags::Zero() | Flags::Carry()});
    }
    SECTION("half carry") {
      CHECK(!Alu::add16(0x0f, 0x00, false).flags.half_carry());
      CHECK(!Alu::add16(0x03, 0x0a, false).flags.half_carry());
      CHECK(!Alu::add16(0x01, 0x0f, false).flags.half_carry());
      CHECK(!Alu::add16(0x0f, 0x0f, false).flags.half_carry());
      CHECK(!Alu::add16(0x0000, 0x7f00, false).flags.overflow());
      CHECK(Alu::add16(0x0100, 0x7f00, false).flags.overflow());
    }
    SECTION("overflow") {
      CHECK(!Alu::add16(0xff, 0x00, false).flags.overflow());
      CHECK(!Alu::add16(0x01, 0x7f, false).flags.overflow());
      CHECK(!Alu::add16(0x00, 0x7f00, false).flags.overflow());
      CHECK(Alu::add16(0x0100, 0x7f00, false).flags.overflow());
    }
  }

  SECTION("8 bit subtraction") {
    // Handy dandy: http://visual6502.org/JSSim/expert-z80.html?steps=60&logmore=f,a,bc&a=0&d=3e000e01910000
    SECTION("no carry in") {
      CHECK(Alu::sub8(0, 0, false) == Alu::R8{0, Flags::Zero() | Flags::Subtract()});
      CHECK(Alu::sub8(1, 0, false) == Alu::R8{1, Flags::Subtract()});
      CHECK(Alu::sub8(0, 1, false) == Alu::R8{0xff, Flags::Sign() | Flags::Subtract() | Flags::Flag5() |
                                                        Flags::Flag3() | Flags::HalfCarry() | Flags::Carry()});
    }
  }

  SECTION("Exclusive or") {
    CHECK(Alu::xor8(0, 0) == Alu::R8{0, Flags::Zero() | Flags::Parity()});
    CHECK(Alu::xor8(0, 0xff) == Alu::R8{0xff, Flags::Sign() | Flags::Flag5() | Flags::Flag3() | Flags::Parity()});
    CHECK(Alu::xor8(1, 0xff) == Alu::R8{0xfe, Flags::Sign() | Flags::Flag5() | Flags::Flag3()});
  }
}

} // namespace specbolt
