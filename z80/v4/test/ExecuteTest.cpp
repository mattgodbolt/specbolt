#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>

#include "z80/v4/Z80.hpp"

#include "peripherals/Memory.hpp"
#include "z80/common/Scheduler.hpp"

// What the shared suite, `z80/test/OpcodeTests.cpp`, does not check of v4: behaviour only v4 models, such as interrupt
// timing, the refresh register and prefix chains, and the rows and effects that suite never looks at. A check both
// would make belongs there, where every implementation has to agree with it.

namespace specbolt::v4 {
namespace {

struct Tester {
  Scheduler scheduler;
  Memory memory{4};
  Z80 z80{scheduler, memory};
  RegisterFile &regs = z80.regs();

  // Each test writes its code at 0, so the stack starts well clear of it, somewhere a test can read back.
  Tester() {
    memory.set_rom_flags({false, false, false, false});
    regs.sp(0x8000);
  }

  void run(auto... bytes) {
    write_to_memory(memory, z80.pc(), bytes...);
    z80.execute_one();
  }
};

} // namespace

TEST_CASE_METHOD(Tester, "Rows the shared suite does not run") {
  SECTION("nop does nothing") {
    regs.set(RegisterFile::R16::HL, 0x1234);
    run(0x00);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1234);
    CHECK_FALSE(z80.halted());
    CHECK(z80.cycle_count() == 4);
  }
  SECTION("logic operations take no carry input") {
    regs.set(RegisterFile::R8::A, 0xf0);
    z80.flags(Flags::Carry());
    run(0xe6, 0x3f); // and 0x3f
    CHECK(regs.get(RegisterFile::R8::A) == 0x30);
    run(0xee, 0xff); // xor 0xff
    CHECK(regs.get(RegisterFile::R8::A) == 0xcf);
    run(0xf6, 0x0f); // or 0x0f
    CHECK(regs.get(RegisterFile::R8::A) == 0xcf);
  }
  SECTION("cp leaves a alone but sets flags") {
    regs.set(RegisterFile::R8::A, 0x42);
    run(0xfe, 0x42); // cp 0x42
    CHECK(regs.get(RegisterFile::R8::A) == 0x42);
    CHECK(z80.flags().zero());
    run(0xfe, 0x43); // cp 0x43
    CHECK(regs.get(RegisterFile::R8::A) == 0x42);
    CHECK(z80.flags().carry());
  }
  SECTION("every opcode decodes to something, including the ed table's filler") {
    run(0xed, 0x00); // a two-byte nop on real hardware
    CHECK(z80.pc() == 2);
  }
}

TEST_CASE_METHOD(Tester, "Effects the shared suite does not check") {
  SECTION("ret takes the address back off the stack") {
    memory.write16(0x7ffe, 0xbeef);
    regs.sp(0x7ffe);
    run(0xc9);
    CHECK(z80.pc() == 0xbeef);
    CHECK(regs.sp() == 0x8000);
    CHECK(z80.cycle_count() == 10);
  }
  SECTION("ret cc takes it off only when it returns") {
    memory.write16(0x7ffe, 0xbeef);
    regs.sp(0x7ffe);
    z80.flags(Flags::Zero());
    run(0xc0); // ret nz, not taken
    CHECK(z80.pc() == 1);
    CHECK(regs.sp() == 0x7ffe);
    CHECK(z80.cycle_count() == 5);

    run(0xc8); // ret z, taken
    CHECK(z80.pc() == 0xbeef);
    CHECK(regs.sp() == 0x8000);
    CHECK(z80.cycle_count() == 5 + 11);
  }
  SECTION("djnz counts b without touching the flags") {
    z80.flags(Flags::Zero() | Flags::Carry());
    regs.set(RegisterFile::R8::B, 2);
    run(0x10, 0x10); // djnz +16, taken
    CHECK(regs.get(RegisterFile::R8::B) == 1);
    CHECK(z80.pc() == 2 + 16);
    CHECK(z80.flags() == (Flags::Zero() | Flags::Carry()));
    CHECK(z80.cycle_count() == 13);

    regs.set(RegisterFile::R8::B, 1);
    run(0x10, 0x10); // now b reaches zero
    CHECK(regs.get(RegisterFile::R8::B) == 0);
    CHECK(z80.cycle_count() == 13 + 8);
  }
  SECTION("in r, (c) reports the byte without disturbing the carry") {
    z80.add_in_handler([](std::uint16_t) { return std::optional<std::uint8_t>{0x00}; });
    z80.flags(Flags::Carry());
    regs.set(RegisterFile::R16::BC, 0x1234);
    run(0xed, 0x48); // in c, (c)
    CHECK(regs.get(RegisterFile::R8::C) == 0x00);
    CHECK(z80.flags() == (Flags::Carry() | Flags::Zero() | Flags::Parity()));
    CHECK(z80.cycle_count() == 12);
  }
}

TEST_CASE_METHOD(Tester, "Interrupts") {
  SECTION("ld a, i and ld a, r report iff2 in the parity flag") {
    // These are the only rows whose operation reads the machine without changing it, a `const` member, so they are
    // where such a member has to keep being called on the machine. Each value's own parity is the opposite of the
    // iff2 it is read under, so an ordinary parity flag would fail both halves.
    regs.i(0x43); // three bits set
    z80.iff2(true);
    run(0xed, 0x57); // ld a, i
    CHECK(regs.get(RegisterFile::R8::A) == 0x43);
    CHECK(z80.flags().parity());
    CHECK(z80.cycle_count() == 9);
    regs.r(0x01); // two opcode fetches later, r is 3: two bits set
    z80.iff2(false);
    run(0xed, 0x5f); // ld a, r
    CHECK(regs.get(RegisterFile::R8::A) == 0x03);
    CHECK_FALSE(z80.flags().parity());
    CHECK(z80.cycle_count() == 18);
  }

  SECTION("ei lets one more instruction run before an interrupt is taken") {
    // `ei ; halt` and `ei ; reti` both depend on this: without it the interrupt arrives before the instruction that was
    // meant to run under it.
    run(0xfb); // ei
    z80.interrupt();
    run(0x00); // nop, runs first
    CHECK(z80.pc() == 2);
    run(0x00); // and now the interrupt is taken instead of this
    CHECK(memory.read16(0x7ffe) == 2); // the address it interrupted
    CHECK(regs.sp() == 0x7ffe);
  }

  SECTION("a halted cycle is a real opcode fetch, so the bus keeps following it") {
    // A halted Z80 goes on fetching, so its four cycles go through `bus` like any other fetch and the address bus
    // follows pc. Idling in `halt` until the frame interrupt is the commonest thing a Spectrum program does, which
    // makes this the place contention most needs to reach.
    regs.pc(0x1234);
    run(0x00); // a nop, to leave the bus somewhere known
    REQUIRE(z80.bus_address() == 0x1234);

    // Halted at a different address than the bus last saw, which is what tells a fetch apart from four cycles of
    // nothing.
    z80.halted(true);
    regs.pc(0x4321);
    const auto before = z80.cycle_count();
    z80.execute_one();
    CHECK(z80.bus_address() == 0x4321);
    CHECK(z80.cycle_count() == before + 4); // the four cycles of an opcode fetch
  }

  SECTION("an interrupt raised while disabled is held, not dropped") {
    // /INT is a level the device holds, not an edge, so arriving during a di/ei window does not lose it.
    z80.iff1(false);
    z80.iff2(false);
    z80.interrupt();
    run(0xfb); // ei
    run(0x00); // nop, deferred by the ei
    CHECK(z80.pc() == 2);
    run(0x00);
    CHECK(memory.read16(0x7ffe) == 2); // held, then taken
    CHECK(regs.sp() == 0x7ffe);
  }
}

TEST_CASE_METHOD(Tester, "The refresh register") {
  SECTION("it counts the acknowledge, and counts through a halt") {
    z80.iff1(true);
    z80.iff2(true);
    regs.r(0);
    run(0x76); // halt
    CHECK(regs.r() == 1);
    CHECK(z80.cycle_count() == 4);

    z80.execute_one(); // halted: an internal nop, which still refreshes
    CHECK(regs.r() == 2);
    CHECK(z80.cycle_count() == 8);

    z80.interrupt();
    z80.execute_one();
    // Two more: the acknowledge is an M1, and the handler's first instruction is fetched in the same call.
    CHECK(regs.r() == 4);
    CHECK_FALSE(z80.halted());
  }

  SECTION("its top bit is not counted") {
    regs.r(0xff);
    run(0x00);
    CHECK(regs.r() == 0x80); // seven bits wrapped, the eighth kept
  }
}

TEST_CASE_METHOD(Tester, "Indexed addressing") {
  SECTION("dd renames hl, and every byte of a prefix chain costs a fetch") {
    regs.set(RegisterFile::R16::IX, 0x12ff);
    regs.set(RegisterFile::R16::HL, 0x1111);
    run(0xdd, 0x23); // inc ix
    CHECK(regs.get(RegisterFile::R16::IX) == 0x1300);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1111);
    CHECK(z80.pc() == 2);
    CHECK(z80.cycle_count() == 10); // 4 + 6

    SECTION("a repeated prefix re-arms rather than recursing") {
      run(0xdd, 0xdd, 0xdd, 0x23);
      CHECK(regs.get(RegisterFile::R16::IX) == 0x1301);
      CHECK(z80.pc() == 6);
      CHECK(z80.cycle_count() == 10 + 4 + 4 + 10);
    }
  }

  SECTION("dd renames h and l as half-registers") {
    regs.set(RegisterFile::R16::IX, 0x1234);
    regs.set(RegisterFile::R16::HL, 0x1111);
    run(0xdd, 0x65); // ld ixh, ixl
    CHECK(regs.get(RegisterFile::R16::IX) == 0x3434);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1111);
    CHECK(z80.cycle_count() == 8);
  }

  SECTION("ld h, (ix+d) keeps the real h") {
    memory.write(0x1236, 0x42);
    regs.set(RegisterFile::R16::IX, 0x1234);
    run(0xdd, 0x66, 0x02); // ld h, (ix+2)
    CHECK(regs.get(RegisterFile::R8::H) == 0x42);
    CHECK(regs.get(RegisterFile::R16::IX) == 0x1234); // ixh untouched
    CHECK(z80.cycle_count() == 19);
  }

  SECTION("inc (ix+d) reads and writes through one address, formed once") {
    memory.write(0x1233, 0x0f);
    regs.set(RegisterFile::R16::IX, 0x1234);
    run(0xdd, 0x34, 0xff); // inc (ix-1)
    CHECK(memory.read(0x1233) == 0x10);
    CHECK(z80.pc() == 3);
    CHECK(z80.cycle_count() == 23);
  }

  SECTION("add a, (ix+d)") {
    memory.write(0x1236, 0x02);
    regs.set(RegisterFile::R8::A, 0x40);
    regs.set(RegisterFile::R16::IX, 0x1234);
    run(0xdd, 0x86, 0x02); // add a, (ix+2)
    CHECK(regs.get(RegisterFile::R8::A) == 0x42);
    CHECK(z80.cycle_count() == 19);
  }

  SECTION("fd is the same table again, through iy") {
    memory.write(0x1236, 0x99);
    regs.set(RegisterFile::R16::IY, 0x1234);
    regs.set(RegisterFile::R16::IX, 0xffff);
    run(0xfd, 0x46, 0x02); // ld b, (iy+2)
    CHECK(regs.get(RegisterFile::R8::B) == 0x99);
    CHECK(z80.cycle_count() == 19);

    SECTION("and the last prefix of a chain wins") {
      regs.set(RegisterFile::R16::IY, 0x12ff);
      run(0xdd, 0xfd, 0x23); // inc iy
      CHECK(regs.get(RegisterFile::R16::IY) == 0x1300);
      CHECK(regs.get(RegisterFile::R16::IX) == 0xffff);
    }
  }

  SECTION("bit n, (ix+d) reads wzh after the memory access, not before") {
    // On the ix page the row reads `test_bit flags <- value=(ix+d) bit={bit:b} flags=flags bus=wzh`, and two of those
    // operands touch the bus: the memory read sets the address that wzh then reports. If the arguments were evaluated
    // in the other order, wzh would report the *previous* address: the opcode fetch, near zero, whose bits 3 and 5 are
    // clear. So choose an index whose high byte has both set, and the two orders differ.
    z80.flags(Flags());
    regs.set(RegisterFile::R16::IX, 0x2834); // +2 -> 0x2836, high byte 0b0010'1000
    memory.write(0x2836, 0xff);
    run(0xdd, 0xcb, 0x02, 0x46); // bit 0, (ix+2)
    CHECK(z80.flags() == (Flags::HalfCarry() | Flags::Flag3() | Flags::Flag5()));
  }

  SECTION("dd cb also copies its result into the register the low bits name") {
    // The undocumented half: v2 and v3 read this as a view over cb and get the wrong answer for every entry where the
    // low bits are not 6.
    regs.set(RegisterFile::R16::IX, 0x1236);
    memory.write(0x1234, 0x00);
    run(0xdd, 0xcb, 0xfe, 0xe0); // set 4, (ix-2), b
    CHECK(memory.read(0x1234) == 0b0001'0000);
    CHECK(regs.get(RegisterFile::R8::B) == 0b0001'0000);
    CHECK(z80.cycle_count() == 23);
  }

  SECTION("rl (ix+d) rotates memory and copies the result out") {
    z80.flags(Flags());
    regs.set(RegisterFile::R16::IX, 0x1236);
    memory.write(0x1234, 0b1111'0101);
    run(0xdd, 0xcb, 0xfe, 0x12); // rl (ix-2), d
    CHECK(memory.read(0x1234) == 0b1110'1010);
    CHECK(regs.get(RegisterFile::R8::D) == 0b1110'1010);
    CHECK(z80.cycle_count() == 23);
  }

  SECTION("fd cb is the same again through iy") {
    regs.set(RegisterFile::R16::IY, 0x1236);
    memory.write(0x1234, 0xff);
    run(0xfd, 0xcb, 0xfe, 0x86); // res 0, (iy-2)
    CHECK(memory.read(0x1234) == 0xfe);
    CHECK(z80.cycle_count() == 23);
  }

  SECTION("ex (sp), ix is not ex (sp), hl") {
    regs.sp(0x9000);
    memory.write16(0x9000, 0x1234);
    regs.set(RegisterFile::R16::IX, 0xbeef);
    regs.set(RegisterFile::R16::HL, 0x1111);
    run(0xdd, 0xe3);
    CHECK(regs.get(RegisterFile::R16::IX) == 0x1234);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1111);
    CHECK(memory.read16(0x9000) == 0xbeef);
    CHECK(z80.cycle_count() == 23);
  }

  SECTION("dd 76 is still halt, because neither override row covers it") {
    run(0xdd, 0x76);
    CHECK(z80.halted());
    CHECK(z80.pc() == 1); // halt rewinds so it re-executes; the dd is not re-read
  }
}

} // namespace specbolt::v4
