#include "z80/Alu.hpp"

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
    SECTION("with carry") {
      SECTION("no carry in") {
        CHECK(Alu::adc16(0, 0, false) == Alu::R16{0, Flags::Zero()});
        CHECK(Alu::adc16(0, 0xff, false) == Alu::R16{0xff, Flags()});
        CHECK(Alu::adc16(1, 0xff, false) == Alu::R16{0x100, Flags()});
        CHECK(Alu::adc16(0xff00, 0xff, false) == Alu::R16{0xffff, Flags::Sign() | Flags::Flag5() | Flags::Flag3()});
        CHECK(Alu::adc16(0xffff, 0, false) == Alu::R16{0xffff, Flags::Sign() | Flags::Flag5() | Flags::Flag3()});
        CHECK(Alu::adc16(0xffff, 1, false) == Alu::R16{0, Flags::Zero() | Flags::HalfCarry() | Flags::Carry()});
      }
      SECTION("with carry in") {
        CHECK(Alu::adc16(0, 0, true) == Alu::R16{1, Flags()});
        CHECK(Alu::adc16(0xff, 0, true) == Alu::R16{0x100, Flags()});
        CHECK(Alu::adc16(0xffff, 0, true) == Alu::R16{0x0, Flags::Zero() | Flags::Carry()});
      }
      SECTION("half carry") {
        CHECK(!Alu::adc16(0x0f, 0x00, false).flags.half_carry());
        CHECK(!Alu::adc16(0x03, 0x0a, false).flags.half_carry());
        CHECK(!Alu::adc16(0x01, 0x0f, false).flags.half_carry());
        CHECK(!Alu::adc16(0x0f, 0x0f, false).flags.half_carry());
        CHECK(!Alu::adc16(0x0fff, 0x0000, false).flags.half_carry());
        CHECK(!Alu::adc16(0x03ff, 0x0a00, false).flags.half_carry());
        CHECK(Alu::adc16(0x01ff, 0x0fff, false).flags.half_carry());
        CHECK(Alu::adc16(0x0fff, 0x0fff, false).flags.half_carry());
        CHECK(Alu::adc16(0x0eff, 0x0101, false).flags.half_carry());
      }
      SECTION("overflow") {
        CHECK(!Alu::adc16(0xff, 0x00, false).flags.overflow());
        CHECK(!Alu::adc16(0x01, 0x7f, false).flags.overflow());
        CHECK(!Alu::adc16(0x00, 0x7f00, false).flags.overflow());
        CHECK(Alu::adc16(0x0100, 0x7f00, false).flags.overflow());
        CHECK(!Alu::adc16(0x0000, 0x7f00, false).flags.overflow());
        CHECK(Alu::adc16(0x0100, 0x7f00, false).flags.overflow());
      }
    }
    SECTION("no carry") {
      // According to z80-heaven: "preserves the S, Z and P/V flags, and H is undefined. Rest of flags modified by
      // definition." Half carry set by the result, it seems.
      SECTION("preserves previous flags") {
        CHECK(Alu::add16(1, 1, Flags::Sign() | Flags::Zero() | Flags::Parity()) ==
              Alu::R16{2, Flags::Sign() | Flags::Zero() | Flags::Parity()});
      }
      SECTION("half carry") {
        CHECK(!Alu::add16(0x0f, 0x00, Flags()).flags.half_carry());
        CHECK(!Alu::add16(0x03, 0x0a, Flags()).flags.half_carry());
        CHECK(!Alu::add16(0x01, 0x0f, Flags()).flags.half_carry());
        CHECK(!Alu::add16(0x0f, 0x0f, Flags()).flags.half_carry());
        CHECK(!Alu::add16(0x0fff, 0x0000, Flags()).flags.half_carry());
        CHECK(!Alu::add16(0x03ff, 0x0a00, Flags()).flags.half_carry());
        CHECK(Alu::add16(0x01ff, 0x0fff, Flags()).flags.half_carry());
        CHECK(Alu::add16(0x0fff, 0x0fff, Flags()).flags.half_carry());
        CHECK(Alu::add16(0x0eff, 0x0101, Flags()).flags.half_carry());
      }
      SECTION("no overflow") {
        CHECK(!Alu::add16(0xff, 0x00, Flags()).flags.overflow());
        CHECK(!Alu::add16(0x01, 0x7f, Flags()).flags.overflow());
        CHECK(!Alu::add16(0x00, 0x7f00, Flags()).flags.overflow());
        CHECK(!Alu::add16(0x0100, 0x7f00, Flags()).flags.overflow());
        CHECK(!Alu::add16(0x0000, 0x7f00, Flags()).flags.overflow());
        CHECK(!Alu::add16(0x0100, 0x7f00, Flags()).flags.overflow());
      }
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

  SECTION("rotates and shifts") {
    SECTION("fast versions") {
      SECTION("carry left") {
        CHECK(Alu::fast_rotate8(0b0000'0001, Alu::Direction::Left, Flags()) == Alu::R8{0b0000'0010, Flags()});
        CHECK(Alu::fast_rotate8(0b0000'0001, Alu::Direction::Left, Flags::Flag3() | Flags::Flag5()) ==
              Alu::R8{0b0000'0010, Flags()});
        CHECK(Alu::fast_rotate8(0b1000'0000, Alu::Direction::Left, Flags::Subtract()) ==
              Alu::R8{0b0000'0000, Flags::Carry()});
        CHECK(Alu::fast_rotate8(0b1000'0000, Alu::Direction::Left, Flags::Carry()) ==
              Alu::R8{0b0000'0001, Flags::Carry()});
      }
      SECTION("carry right") {
        CHECK(Alu::fast_rotate8(0b0000'0001, Alu::Direction::Right, Flags()) == Alu::R8{0b0000'0000, Flags::Carry()});
        CHECK(Alu::fast_rotate8(0b0000'0001, Alu::Direction::Right, Flags::Flag3() | Flags::Flag5()) ==
              Alu::R8{0b0000'0000, Flags::Carry()});
        CHECK(
            Alu::fast_rotate8(0b1000'0000, Alu::Direction::Right, Flags::Subtract()) == Alu::R8{0b0100'0000, Flags()});
        CHECK(Alu::fast_rotate8(0b1000'0000, Alu::Direction::Right, Flags::Carry()) == Alu::R8{0b1100'0000, Flags()});
      }
      SECTION("circular left") {
        CHECK(Alu::fast_rotate_circular8(0b0000'0001, Alu::Direction::Left, Flags()) == Alu::R8{0b0000'0010, Flags()});
        CHECK(Alu::fast_rotate_circular8(0b0000'0001, Alu::Direction::Left, Flags::Flag3() | Flags::Flag5()) ==
              Alu::R8{0b0000'0010, Flags()});
        CHECK(Alu::fast_rotate_circular8(0b1000'0000, Alu::Direction::Left, Flags::Subtract()) ==
              Alu::R8{0b0000'0001, Flags::Carry()});
        CHECK(Alu::fast_rotate_circular8(0b1000'0000, Alu::Direction::Left, Flags::Carry()) ==
              Alu::R8{0b0000'0001, Flags::Carry()});
      }
      SECTION("circular right") {
        CHECK(Alu::fast_rotate_circular8(0b0000'0001, Alu::Direction::Right, Flags()) ==
              Alu::R8{0b1000'0000, Flags::Carry()});
        CHECK(Alu::fast_rotate_circular8(0b0000'0001, Alu::Direction::Right, Flags::Flag3() | Flags::Flag5()) ==
              Alu::R8{0b1000'0000, Flags::Carry()});
        CHECK(Alu::fast_rotate_circular8(0b1000'0000, Alu::Direction::Right, Flags::Subtract()) ==
              Alu::R8{0b0100'0000, Flags()});
        CHECK(Alu::fast_rotate_circular8(0b1000'0000, Alu::Direction::Right, Flags::Carry()) ==
              Alu::R8{0b0100'0000, Flags()});
      }
    }
    SECTION("normal versions") {
      SECTION("rotate left") {
        CHECK(Alu::rotate8(0b0000'0001, Alu::Direction::Left, false) == Alu::R8{0b0000'0010, Flags()});
        CHECK(Alu::rotate8(0b1000'0000, Alu::Direction::Left, false) ==
              Alu::R8{0b0000'0000, Flags::Carry() | Flags::Zero() | Flags::Parity()});
        CHECK(Alu::rotate8(0b1000'0000, Alu::Direction::Left, true) == Alu::R8{0b0000'0001, Flags::Carry()});
        CHECK(Alu::rotate8(0b1111'1111, Alu::Direction::Left, true) ==
              Alu::R8{0b1111'1111, Flags::Carry() | Flags::Sign() | Flags::Flag3() | Flags::Flag5() | Flags::Parity()});
      }
      SECTION("rotate right") {
        CHECK(Alu::rotate8(0b0000'0001, Alu::Direction::Right, false) ==
              Alu::R8{0b0000'0000, Flags::Zero() | Flags::Carry() | Flags::Parity()});
        CHECK(Alu::rotate8(0b1000'0000, Alu::Direction::Right, false) == Alu::R8{0b0100'0000, Flags()});
        CHECK(Alu::rotate8(0b0000'0000, Alu::Direction::Right, true) == Alu::R8{0b1000'0000, Flags::Sign()});
      }
      SECTION("rotate circular left") {
        CHECK(Alu::rotate_circular8(0b0000'0000, Alu::Direction::Left) ==
              Alu::R8{0b0000'0000, Flags::Zero() | Flags::Parity()});
        CHECK(Alu::rotate_circular8(0b1111'1111, Alu::Direction::Left) ==
              Alu::R8{0b1111'1111, Flags::Carry() | Flags::Flag3() | Flags::Flag5() | Flags::Sign() | Flags::Parity()});
        CHECK(Alu::rotate_circular8(0b0000'0001, Alu::Direction::Left) == Alu::R8{0b0000'0010, Flags()});
        CHECK(Alu::rotate_circular8(0b0100'0000, Alu::Direction::Left) == Alu::R8{0b1000'0000, Flags::Sign()});
        CHECK(Alu::rotate_circular8(0b1000'0000, Alu::Direction::Left) == Alu::R8{0b0000'0001, Flags::Carry()});
      }
      SECTION("rotate circular right") {
        CHECK(Alu::rotate_circular8(0b0000'0000, Alu::Direction::Right) ==
              Alu::R8{0b0000'0000, Flags::Zero() | Flags::Parity()});
        CHECK(Alu::rotate_circular8(0b1111'1111, Alu::Direction::Right) ==
              Alu::R8{0b1111'1111, Flags::Carry() | Flags::Flag3() | Flags::Flag5() | Flags::Sign() | Flags::Parity()});
        CHECK(Alu::rotate_circular8(0b0000'0001, Alu::Direction::Right) ==
              Alu::R8{0b1000'0000, Flags::Sign() | Flags::Carry()});
        CHECK(Alu::rotate_circular8(0b1000'0000, Alu::Direction::Right) == Alu::R8{0b0100'0000, Flags()});
        CHECK(Alu::rotate_circular8(0b0001'0000, Alu::Direction::Right) == Alu::R8{0b0000'1000, Flags::Flag3()});
      }
      SECTION("shift left arithmetic") {
        CHECK(Alu::shift_arithmetic8(0b0101'1010, Alu::Direction::Left) ==
              Alu::R8{0b1011'0100, Flags::Sign() | Flags::Flag5() | Flags::Parity()});
        CHECK(Alu::shift_arithmetic8(0b1010'0101, Alu::Direction::Left) ==
              Alu::R8{0b0100'1010, Flags::Carry() | Flags::Flag3()});
        CHECK(Alu::shift_arithmetic8(0b0000'0000, Alu::Direction::Left) ==
              Alu::R8{0b0000'0000, Flags::Zero() | Flags::Parity()});
        CHECK(Alu::shift_arithmetic8(0b1111'1111, Alu::Direction::Left) ==
              Alu::R8{0b1111'1110, Flags::Sign() | Flags::Flag3() | Flags::Flag5() | Flags::Carry()});
      }
      SECTION("shift right arithmetic") {
        CHECK(Alu::shift_arithmetic8(0b0101'1010, Alu::Direction::Right) ==
              Alu::R8{0b001'01101, Flags::Flag3() | Flags::Flag5() | Flags::Parity()});
        CHECK(Alu::shift_arithmetic8(0b1010'0101, Alu::Direction::Right) ==
              Alu::R8{0b1101'0010, Flags::Carry() | Flags::Sign() | Flags::Parity()});
        CHECK(Alu::shift_arithmetic8(0b0000'0000, Alu::Direction::Right) ==
              Alu::R8{0b0000'0000, Flags::Zero() | Flags::Parity()});
        CHECK(Alu::shift_arithmetic8(0b1111'1111, Alu::Direction::Right) ==
              Alu::R8{0b1111'1111, Flags::Sign() | Flags::Flag3() | Flags::Flag5() | Flags::Carry() | Flags::Parity()});
      }
      SECTION("shift right logical") {
        CHECK(Alu::shift_logical8(0b0101'1010, Alu::Direction::Right) ==
              Alu::R8{0b0010'1101, Flags::Flag3() | Flags::Flag5() | Flags::Parity()});
        CHECK(Alu::shift_logical8(0b1010'0101, Alu::Direction::Right) == Alu::R8{0b0101'0010, Flags::Carry()});
        CHECK(Alu::shift_logical8(0b0000'0000, Alu::Direction::Right) ==
              Alu::R8{0b0000'0000, Flags::Zero() | Flags::Parity()});
        CHECK(Alu::shift_logical8(0b1111'1111, Alu::Direction::Right) ==
              Alu::R8{0b0111'1111, Flags::Flag3() | Flags::Flag5() | Flags::Carry()});
      }
      SECTION("shift left logical") {
        CHECK(Alu::shift_logical8(0b0101'1010, Alu::Direction::Left) ==
              Alu::R8{0b1011'0101, Flags::Sign() | Flags::Flag5()});
        CHECK(Alu::shift_logical8(0b1010'0101, Alu::Direction::Left) ==
              Alu::R8{0b0100'1011, Flags::Carry() | Flags::Flag3() | Flags::Parity()});
        CHECK(Alu::shift_logical8(0b0000'0000, Alu::Direction::Left) == Alu::R8{0b0000'0001, Flags()});
        CHECK(Alu::shift_logical8(0b1111'1111, Alu::Direction::Left) ==
              Alu::R8{0b1111'1111, Flags::Sign() | Flags::Flag3() | Flags::Flag5() | Flags::Carry() | Flags::Parity()});
      }
    }
  }

  SECTION("daa") {
    SECTION("from test case") { CHECK(Alu::daa(0x8e, Flags(0x04)) == Alu::R8{0x94, Flags(0x90)}); }
    SECTION("no adjustment") {
      CHECK(Alu::daa(0, Flags()) == Alu::R8{0, Flags::Zero() | Flags::Parity()});
      CHECK(Alu::daa(1, Flags()) == Alu::R8{1, Flags()});
    }
    SECTION("addition") {
      CHECK(Alu::daa(0, Flags::HalfCarry()) == Alu::R8{0x06, Flags::Parity()});
      CHECK(Alu::daa(0xa, Flags()) == Alu::R8{0x10, Flags::HalfCarry()});
      CHECK(Alu::daa(0xaa, Flags()) == Alu::R8{0x10, Flags::Carry() | Flags::HalfCarry()});
      CHECK(Alu::daa(0, Flags::HalfCarry()) == Alu::R8{0x06, Flags::Parity()});
      CHECK(Alu::daa(0xa0, Flags::HalfCarry()) == Alu::R8{0x06, Flags::Parity() | Flags::Carry()});
    }
    SECTION("subtraction") {
      CHECK(Alu::daa(0, Flags::Subtract() | Flags::HalfCarry()) ==
            Alu::R8{0xfa, Flags::Subtract() | Flags::Parity() | Flags::Flag5() | Flags::Flag3() | Flags::HalfCarry() |
                              Flags::Sign()});
      CHECK(Alu::daa(0xf, Flags::Subtract()) == Alu::R8{0x09, Flags::Subtract() | Flags::Flag3() | Flags::Parity()});
      CHECK(Alu::daa(0xff, Flags::Subtract()) ==
            Alu::R8{0x99, Flags::Subtract() | Flags::Sign() | Flags::Parity() | Flags::Carry() | Flags::Flag3()});
      CHECK(Alu::daa(0, Flags::Subtract() | Flags::HalfCarry()) ==
            Alu::R8{0xfa, Flags::Subtract() | Flags::Sign() | Flags::Flag5() | Flags::HalfCarry() | Flags::Flag3() |
                              Flags::Parity()});
      CHECK(Alu::daa(0xf0, Flags::Subtract() | Flags::HalfCarry()) ==
            Alu::R8{
                0x8a, Flags::Flags::Subtract() | Flags::Sign() | Flags::HalfCarry() | Flags::Flag3() | Flags::Carry()});
    }
    SECTION("preserves Carry") {
      CHECK(Alu::daa(0, Flags::Carry()) == Alu::R8{0x60, Flags::Parity() | Flags::Flag5() | Flags::Carry()});
    }
  }

  // TODO: Fix this half-carry for sbc
  // SECTION("sbc16") {
  //   SECTION("from elite test case") { CHECK(Alu::sbc16(0, 0, false) == Alu::R16{0, Flags(0x42)}); }
  // }
}

} // namespace specbolt
