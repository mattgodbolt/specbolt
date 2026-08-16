#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>

#include "z80/v4/Z80.hpp"

#include "peripherals/Memory.hpp"
#include "z80/common/Scheduler.hpp"

// Cycle counts and results are the ones `z80/test/OpcodeTests.cpp` asserts
// against v1/v2/v3, so a row that decodes has to agree with three
// implementations that already pass zexdoc.

namespace specbolt::v4 {
namespace {

struct Tester {
  Scheduler scheduler;
  Memory memory{4};
  Z80 z80{scheduler, memory};
  RegisterFile &regs = z80.regs();

  Tester() { memory.set_rom_flags({false, false, false, false}); }

  void run(auto... bytes) {
    write_to_memory(memory, z80.pc(), bytes...);
    z80.execute_one();
  }
};

} // namespace

TEST_CASE("Conditions") {
  Tester t;
  auto &regs = t.regs;
  regs.sp(0x8000);

  SECTION("jp cc costs the same either way") {
    t.z80.flags(Flags::Zero());
    t.run(0xc2, 0x34, 0x12); // jp nz, 0x1234, not taken
    CHECK(t.z80.pc() == 3);
    CHECK(t.z80.cycle_count() == 10);
    t.run(0xca, 0x34, 0x12); // jp z, 0x1234, taken
    CHECK(t.z80.pc() == 0x1234);
    CHECK(t.z80.cycle_count() == 20);
  }
  SECTION("call cc pays for the push only when it takes it") {
    t.z80.flags(Flags());
    t.run(0xd4, 0x34, 0x12); // call nc, 0x1234, taken
    CHECK(t.z80.pc() == 0x1234);
    CHECK(regs.sp() == 0x7ffe);
    CHECK(t.z80.cycle_count() == 17);
  }
  SECTION("call cc not taken is ten") {
    t.z80.flags(Flags::Carry());
    t.run(0xd4, 0x34, 0x12); // call nc, not taken
    CHECK(t.z80.pc() == 3);
    CHECK(regs.sp() == 0x8000);
    CHECK(t.z80.cycle_count() == 10);
  }
  SECTION("ret cc is eleven taken and five not") {
    t.memory.write16(0x7ffe, 0xbeef);
    regs.sp(0x7ffe);
    t.z80.flags(Flags::Zero());
    t.run(0xc0); // ret nz, not taken
    CHECK(t.z80.pc() == 1);
    CHECK(regs.sp() == 0x7ffe);
    CHECK(t.z80.cycle_count() == 5);

    t.run(0xc8); // ret z, taken
    CHECK(t.z80.pc() == 0xbeef);
    CHECK(regs.sp() == 0x8000);
    CHECK(t.z80.cycle_count() == 5 + 11);
  }
  SECTION("jr is twelve taken and seven not") {
    t.z80.flags(Flags::Zero());
    t.run(0x20, 0x10); // jr nz, +16, not taken
    CHECK(t.z80.pc() == 2);
    CHECK(t.z80.cycle_count() == 7);
    t.run(0x28, 0x10); // jr z, +16, taken
    CHECK(t.z80.pc() == 2 + 2 + 16);
    CHECK(t.z80.cycle_count() == 7 + 12);
  }
  SECTION("jr measures backwards from the end of the instruction") {
    t.run(0x18, 0xfe); // jr -2: a tight loop onto itself
    CHECK(t.z80.pc() == 0);
    CHECK(t.z80.cycle_count() == 12);
  }
  SECTION("djnz counts b without touching the flags") {
    t.z80.flags(Flags::Zero() | Flags::Carry());
    regs.set(RegisterFile::R8::B, 2);
    t.run(0x10, 0x10); // djnz +16, taken
    CHECK(regs.get(RegisterFile::R8::B) == 1);
    CHECK(t.z80.pc() == 2 + 16);
    CHECK(t.z80.flags() == (Flags::Zero() | Flags::Carry()));
    CHECK(t.z80.cycle_count() == 13);

    regs.set(RegisterFile::R8::B, 1);
    t.run(0x10, 0x10); // now b reaches zero
    CHECK(regs.get(RegisterFile::R8::B) == 0);
    CHECK(t.z80.cycle_count() == 13 + 8);
  }
}

TEST_CASE("Ports and the odd exchange") {
  Tester t;
  auto &regs = t.regs;

  SECTION("out (n), a puts the accumulator on both halves of the port") {
    std::uint16_t seen_port = 0;
    std::uint8_t seen_value = 0;
    t.z80.add_out_handler([&](const std::uint16_t port, const std::uint8_t value) {
      seen_port = port;
      seen_value = value;
    });
    regs.set(RegisterFile::R8::A, 0x7f);
    t.run(0xd3, 0xfe); // out (0xfe), a
    CHECK(seen_port == 0x7ffe);
    CHECK(seen_value == 0x7f);
    CHECK(t.z80.cycle_count() == 11);
  }
  SECTION("in a, (n)") {
    t.z80.add_in_handler([](std::uint16_t) { return std::optional<std::uint8_t>{0xa5}; });
    regs.set(RegisterFile::R8::A, 0x12);
    t.run(0xdb, 0x34); // in a, (0x34)
    CHECK(regs.get(RegisterFile::R8::A) == 0xa5);
    CHECK(t.z80.cycle_count() == 11);
  }
  SECTION("ex (sp), hl") {
    regs.sp(0x9000);
    t.memory.write16(0x9000, 0x1234);
    regs.set(RegisterFile::R16::HL, 0xbeef);
    t.run(0xe3);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1234);
    CHECK(t.memory.read16(0x9000) == 0xbeef);
    CHECK(t.z80.cycle_count() == 19);
  }
}

TEST_CASE("Stack, jumps and exchanges") {
  Tester t;
  auto &regs = t.regs;
  regs.sp(0x8000);

  SECTION("push and pop are the stack pointer and a sixteen-bit access") {
    regs.set(RegisterFile::R16::BC, 0x1234);
    t.run(0xc5); // push bc
    CHECK(regs.sp() == 0x7ffe);
    CHECK(t.memory.read16(0x7ffe) == 0x1234);
    CHECK(t.z80.cycle_count() == 11);

    t.run(0xe1); // pop hl
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1234);
    CHECK(regs.sp() == 0x8000);
    CHECK(t.z80.cycle_count() == 11 + 10);
  }
  SECTION("call pushes the address after the instruction") {
    t.run(0xcd, 0x34, 0x12); // call 0x1234
    CHECK(t.z80.pc() == 0x1234);
    CHECK(regs.sp() == 0x7ffe);
    CHECK(t.memory.read16(0x7ffe) == 3);
    CHECK(t.z80.cycle_count() == 17);
  }
  SECTION("ret takes it back off") {
    t.memory.write16(0x7ffe, 0xbeef);
    regs.sp(0x7ffe);
    t.run(0xc9);
    CHECK(t.z80.pc() == 0xbeef);
    CHECK(regs.sp() == 0x8000);
    CHECK(t.z80.cycle_count() == 10);
  }
  SECTION("rst is a call to a constant the opcode names") {
    t.run(0xff); // rst 0x38
    CHECK(t.z80.pc() == 0x38);
    CHECK(t.memory.read16(0x7ffe) == 1);
    CHECK(t.z80.cycle_count() == 11);
  }
  SECTION("jp and jp (hl)") {
    t.run(0xc3, 0x34, 0x12);
    CHECK(t.z80.pc() == 0x1234);
    CHECK(t.z80.cycle_count() == 10);

    regs.set(RegisterFile::R16::HL, 0x4321);
    t.run(0xe9);
    CHECK(t.z80.pc() == 0x4321);
    CHECK(t.z80.cycle_count() == 14);
  }
  SECTION("ld sp, hl") {
    regs.set(RegisterFile::R16::HL, 0x1234);
    t.run(0xf9);
    CHECK(regs.sp() == 0x1234);
    CHECK(t.z80.cycle_count() == 6);
  }
  SECTION("sixteen-bit loads through an immediate address") {
    regs.set(RegisterFile::R16::HL, 0xbeef);
    t.run(0x22, 0x00, 0x90); // ld (0x9000), hl
    CHECK(t.memory.read16(0x9000) == 0xbeef);
    CHECK(t.z80.cycle_count() == 16);

    t.run(0x2a, 0x00, 0x90); // ld hl, (0x9000)
    CHECK(regs.get(RegisterFile::R16::HL) == 0xbeef);
    CHECK(t.z80.cycle_count() == 32);
  }
  SECTION("the exchanges") {
    regs.set(RegisterFile::R16::DE, 0x1111);
    regs.set(RegisterFile::R16::HL, 0x2222);
    t.run(0xeb); // ex de, hl
    CHECK(regs.get(RegisterFile::R16::DE) == 0x2222);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1111);
    CHECK(t.z80.cycle_count() == 4);

    t.run(0xd9); // exx
    CHECK(regs.get(RegisterFile::R16::HL) == 0xffff);
    t.run(0xd9);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1111);
  }
  SECTION("di and ei move both interrupt flip-flops") {
    t.run(0xfb); // ei
    CHECK(t.z80.iff1());
    CHECK(t.z80.iff2());
    t.run(0xf3); // di
    CHECK_FALSE(t.z80.iff1());
    CHECK_FALSE(t.z80.iff2());
  }
  SECTION("add hl, bc") {
    regs.set(RegisterFile::R16::HL, 0x1234);
    regs.set(RegisterFile::R16::BC, 0x1111);
    t.run(0x09);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x2345);
    CHECK(t.z80.cycle_count() == 11);
  }
  SECTION("dd makes add hl, bc into add ix, bc") {
    regs.set(RegisterFile::R16::IX, 0x1234);
    regs.set(RegisterFile::R16::BC, 0x1111);
    t.run(0xdd, 0x09);
    CHECK(regs.get(RegisterFile::R16::IX) == 0x2345);
    CHECK(t.z80.cycle_count() == 15);
  }
}

TEST_CASE("Interrupts") {
  Tester t;
  auto &regs = t.regs;
  regs.sp(0x8000);

  SECTION("in r, (c) reports the byte without disturbing the carry") {
    t.z80.add_in_handler([](std::uint16_t) { return std::optional<std::uint8_t>{0x00}; });
    t.z80.flags(Flags::Carry());
    regs.set(RegisterFile::R16::BC, 0x1234);
    t.run(0xed, 0x48); // in c, (c)
    CHECK(regs.get(RegisterFile::R8::C) == 0x00);
    CHECK(t.z80.flags() == (Flags::Carry() | Flags::Zero() | Flags::Parity()));
    CHECK(t.z80.cycle_count() == 12);
  }

  SECTION("ei lets one more instruction run before an interrupt is taken") {
    // `ei ; halt` and `ei ; reti` both depend on this: without it the interrupt
    // arrives before the instruction that was meant to run under it.
    t.run(0xfb); // ei
    t.z80.interrupt();
    t.run(0x00); // nop, runs first
    CHECK(t.z80.pc() == 2);
    t.run(0x00); // and now the interrupt is taken instead of this
    CHECK(t.memory.read16(0x7ffe) == 2); // the address it interrupted
    CHECK(regs.sp() == 0x7ffe);
  }

  SECTION("the refresh register counts the acknowledge, and counts through a halt") {
    t.z80.iff1(true);
    t.z80.iff2(true);
    t.regs.r(0);
    t.run(0x76); // halt
    CHECK(t.regs.r() == 1);
    CHECK(t.z80.cycle_count() == 4);

    t.z80.execute_one(); // halted: an internal nop, which still refreshes
    CHECK(t.regs.r() == 2);
    CHECK(t.z80.cycle_count() == 8);

    t.z80.interrupt();
    t.z80.execute_one();
    // Two more: the acknowledge is an M1, and the handler's first instruction
    // is fetched in the same call.
    CHECK(t.regs.r() == 4);
    CHECK_FALSE(t.z80.halted());
  }

  SECTION("a halted cycle is a real opcode fetch, so the bus keeps following it") {
    // The halted path once spent its four cycles directly rather than through
    // `bus`, which left the address bus holding whatever the last instruction
    // put there for as long as the machine idled, and idling in `halt` until
    // the frame interrupt is the commonest thing a Spectrum program does. It
    // also meant the one place contention would matter most was the one place
    // the seam did not reach.
    t.regs.pc(0x1234);
    t.run(0x00); // a nop, to leave the bus somewhere known
    REQUIRE(t.z80.bus_address() == 0x1234);

    // Halted at a different address than the bus last saw, which is what tells
    // a fetch apart from four cycles of nothing.
    t.z80.halted(true);
    t.regs.pc(0x4321);
    const auto before = t.z80.cycle_count();
    t.z80.execute_one();
    CHECK(t.z80.bus_address() == 0x4321);
    CHECK(t.z80.cycle_count() == before + 4); // an opcode cycle, as before
  }

  SECTION("the refresh register's top bit is not counted") {
    t.regs.r(0xff);
    t.run(0x00);
    CHECK(t.regs.r() == 0x80); // seven bits wrapped, the eighth kept
  }

  SECTION("an interrupt raised while disabled is held, not dropped") {
    // /INT is a level the device holds, not an edge, so arriving during a
    // di/ei window does not lose it.
    t.z80.iff1(false);
    t.z80.iff2(false);
    t.z80.interrupt();
    t.run(0xfb); // ei
    t.run(0x00); // nop, deferred by the ei
    CHECK(t.z80.pc() == 2);
    t.run(0x00);
    CHECK(t.memory.read16(0x7ffe) == 2); // held, then taken
    CHECK(regs.sp() == 0x7ffe);
  }
}

TEST_CASE("Rotates and shifts") {
  Tester t;
  auto &regs = t.regs;
  t.z80.flags(Flags());
  regs.set(RegisterFile::R8::A, 0b1000'0001);
  regs.set(RegisterFile::R16::HL, 0x1234);
  t.memory.write(0x1234, 0b1100'0011);

  SECTION("rlca keeps sign, zero and parity where rlc a recomputes them") {
    t.z80.flags(Flags::Carry());
    regs.set(RegisterFile::R8::A, 0x59);
    t.run(0x07); // rlca
    CHECK(regs.get(RegisterFile::R8::A) == 0xb2);
    CHECK(t.z80.flags() == Flags::Flag5());
    CHECK(t.z80.cycle_count() == 4);
  }
  SECTION("rlc a") {
    t.run(0xcb, 0x07);
    CHECK(regs.get(RegisterFile::R8::A) == 0b0000'0011);
    CHECK(t.z80.flags() == (Flags::Parity() | Flags::Carry()));
    CHECK(t.z80.cycle_count() == 8);
  }
  SECTION("rlc (hl)") {
    t.run(0xcb, 0x06);
    CHECK(t.memory.read(0x1234) == 0b1000'0111);
    CHECK(t.z80.flags() == (Flags::Sign() | Flags::Parity() | Flags::Carry()));
    CHECK(t.z80.cycle_count() == 15);
  }
  SECTION("rl a takes the carry in") {
    t.run(0xcb, 0x17);
    CHECK(regs.get(RegisterFile::R8::A) == 0b0000'0010);
    CHECK(t.z80.flags() == Flags::Carry());
    CHECK(t.z80.cycle_count() == 8);
  }
  SECTION("rl (ix+d) rotates memory and copies the result out") {
    regs.set(RegisterFile::R16::IX, 0x1236);
    t.memory.write(0x1234, 0b1111'0101);
    t.run(0xdd, 0xcb, 0xfe, 0x12); // rl (ix-2), d
    CHECK(t.memory.read(0x1234) == 0b1110'1010);
    CHECK(regs.get(RegisterFile::R8::D) == 0b1110'1010);
    CHECK(t.z80.cycle_count() == 23);
  }
  SECTION("rlc (ix+d) with the low bits 6 copies nowhere") {
    regs.set(RegisterFile::R16::IX, 0x1236);
    t.memory.write(0x1234, 0b1111'0101);
    t.run(0xdd, 0xcb, 0xfe, 0x06); // rlc (ix-2)
    CHECK(t.memory.read(0x1234) == 0b1110'1011);
    CHECK(t.z80.cycle_count() == 23);
  }
}

TEST_CASE("Indexed addressing") {
  Tester t;
  auto &regs = t.regs;

  SECTION("dd renames hl, and every byte of a prefix chain costs a fetch") {
    regs.set(RegisterFile::R16::IX, 0x12ff);
    regs.set(RegisterFile::R16::HL, 0x1111);
    t.run(0xdd, 0x23); // inc ix
    CHECK(regs.get(RegisterFile::R16::IX) == 0x1300);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1111);
    CHECK(t.z80.pc() == 2);
    CHECK(t.z80.cycle_count() == 10); // 4 + 6

    SECTION("a repeated prefix re-arms rather than recursing") {
      t.run(0xdd, 0xdd, 0xdd, 0x23);
      CHECK(regs.get(RegisterFile::R16::IX) == 0x1301);
      CHECK(t.z80.pc() == 6);
      CHECK(t.z80.cycle_count() == 10 + 4 + 4 + 10);
    }
  }

  SECTION("dd on an instruction that never mentions hl is a 4-cycle no-op") {
    regs.set(RegisterFile::R8::B, 0x40);
    t.run(0xdd, 0x04); // inc b
    CHECK(regs.get(RegisterFile::R8::B) == 0x41);
    CHECK(t.z80.pc() == 2);
    CHECK(t.z80.cycle_count() == 8);
  }

  SECTION("dd renames h and l as half-registers") {
    regs.set(RegisterFile::R16::IX, 0x1234);
    regs.set(RegisterFile::R16::HL, 0x1111);
    t.run(0xdd, 0x65); // ld ixh, ixl
    CHECK(regs.get(RegisterFile::R16::IX) == 0x3434);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1111);
    CHECK(t.z80.cycle_count() == 8);
  }

  SECTION("ld b, (ix+d)") {
    t.memory.write(0x122f, 0xcc);
    regs.set(RegisterFile::R16::IX, 0x1234);
    t.run(0xdd, 0x46, 0xfb); // ld b, (ix-5)
    CHECK(regs.get(RegisterFile::R8::B) == 0xcc);
    CHECK(t.z80.pc() == 3);
    CHECK(t.z80.cycle_count() == 19);
  }

  SECTION("ld (ix+d), h keeps the real h") {
    regs.set(RegisterFile::R8::H, 0xbe);
    regs.set(RegisterFile::R16::IX, 0x1234);
    t.run(0xdd, 0x74, 0x02); // ld (ix+2), h
    CHECK(t.memory.read(0x1236) == 0xbe);
    CHECK(t.z80.pc() == 3);
    CHECK(t.z80.cycle_count() == 19);
  }

  SECTION("ld h, (ix+d) keeps the real h too") {
    t.memory.write(0x1236, 0x42);
    regs.set(RegisterFile::R16::IX, 0x1234);
    t.run(0xdd, 0x66, 0x02); // ld h, (ix+2)
    CHECK(regs.get(RegisterFile::R8::H) == 0x42);
    CHECK(regs.get(RegisterFile::R16::IX) == 0x1234); // ixh untouched
    CHECK(t.z80.cycle_count() == 19);
  }

  SECTION("inc (ix+d) reads and writes through one address, formed once") {
    t.memory.write(0x1233, 0x0f);
    regs.set(RegisterFile::R16::IX, 0x1234);
    t.run(0xdd, 0x34, 0xff); // inc (ix-1)
    CHECK(t.memory.read(0x1233) == 0x10);
    CHECK(t.z80.pc() == 3);
    CHECK(t.z80.cycle_count() == 23);
  }

  SECTION("ld (ix+d), n reads its immediate inside the address window") {
    regs.set(RegisterFile::R16::IX, 0x1234);
    t.run(0xdd, 0x36, 0x02, 0x12); // ld (ix+2), 0x12
    CHECK(t.memory.read(0x1236) == 0x12);
    CHECK(t.z80.pc() == 4);
    CHECK(t.z80.cycle_count() == 19); // not 22: the n read is part of the five
  }

  SECTION("add a, (ix+d)") {
    t.memory.write(0x1236, 0x02);
    regs.set(RegisterFile::R8::A, 0x40);
    regs.set(RegisterFile::R16::IX, 0x1234);
    t.run(0xdd, 0x86, 0x02); // add a, (ix+2)
    CHECK(regs.get(RegisterFile::R8::A) == 0x42);
    CHECK(t.z80.cycle_count() == 19);
  }

  SECTION("fd is the same table again, through iy") {
    t.memory.write(0x1236, 0x99);
    regs.set(RegisterFile::R16::IY, 0x1234);
    regs.set(RegisterFile::R16::IX, 0xffff);
    t.run(0xfd, 0x46, 0x02); // ld b, (iy+2)
    CHECK(regs.get(RegisterFile::R8::B) == 0x99);
    CHECK(t.z80.cycle_count() == 19);

    SECTION("and the last prefix of a chain wins") {
      regs.set(RegisterFile::R16::IY, 0x12ff);
      t.run(0xdd, 0xfd, 0x23); // inc iy
      CHECK(regs.get(RegisterFile::R16::IY) == 0x1300);
      CHECK(regs.get(RegisterFile::R16::IX) == 0xffff);
    }
  }

  SECTION("dd cb reads its displacement before the byte that says what to do") {
    regs.set(RegisterFile::R16::IX, 0x1236);
    t.memory.write(0x1234, 0x00);
    t.run(0xdd, 0xcb, 0xfe, 0xe6); // set 4, (ix-2)
    CHECK(t.memory.read(0x1234) == 0b0001'0000);
    CHECK(t.z80.pc() == 4);
    CHECK(t.z80.cycle_count() == 23);
  }

  SECTION("bit n, (ix+d) reads wzh after the memory access, not before") {
    // `bit8 flags <- (ix+d) {b} flags wzh` has two operands that touch the bus:
    // the memory read sets the address wzh then reports. If the arguments were
    // evaluated in the other order, wzh would report the *previous* address:
    // the opcode fetch, near zero, whose bits 3 and 5 are clear. So choose an
    // index whose high byte has both set, and the two orders differ.
    t.z80.flags(Flags());
    regs.set(RegisterFile::R16::IX, 0x2834); // +2 -> 0x2836, high byte 0b0010'1000
    t.memory.write(0x2836, 0xff);
    t.run(0xdd, 0xcb, 0x02, 0x46); // bit 0, (ix+2)
    CHECK(t.z80.flags() == (Flags::HalfCarry() | Flags::Flag3() | Flags::Flag5()));
  }

  SECTION("bit n, (ix+d) takes flags 3 and 5 from the address, not a register") {
    t.z80.flags(Flags());
    regs.set(RegisterFile::R8::H, 0xff); // would set both if the row named h
    regs.set(RegisterFile::R16::IX, 0x1234);
    t.memory.write(0x1236, 0x00);
    t.run(0xdd, 0xcb, 0x02, 0x46); // bit 0, (ix+2)
    CHECK(t.z80.pc() == 4);
    CHECK(t.z80.cycle_count() == 20);
    CHECK(t.z80.flags() == (Flags::Zero() | Flags::HalfCarry() | Flags::Parity()));

    t.memory.write(0x1236, 0x01);
    t.run(0xdd, 0xcb, 0x02, 0x46);
    CHECK(t.z80.flags() == Flags::HalfCarry());
  }

  SECTION("dd cb also copies its result into the register the low bits name") {
    // The undocumented half: v2 and v3 read this as a view over cb and get the
    // wrong answer for every entry where the low bits are not 6.
    regs.set(RegisterFile::R16::IX, 0x1236);
    t.memory.write(0x1234, 0x00);
    t.run(0xdd, 0xcb, 0xfe, 0xe0); // set 4, (ix-2), b
    CHECK(t.memory.read(0x1234) == 0b0001'0000);
    CHECK(regs.get(RegisterFile::R8::B) == 0b0001'0000);
    CHECK(t.z80.cycle_count() == 23);
  }

  SECTION("fd cb is the same again through iy") {
    regs.set(RegisterFile::R16::IY, 0x1236);
    t.memory.write(0x1234, 0xff);
    t.run(0xfd, 0xcb, 0xfe, 0x86); // res 0, (iy-2)
    CHECK(t.memory.read(0x1234) == 0xfe);
    CHECK(t.z80.cycle_count() == 23);
  }

  SECTION("ex (sp), ix is not ex (sp), hl") {
    regs.sp(0x9000);
    t.memory.write16(0x9000, 0x1234);
    regs.set(RegisterFile::R16::IX, 0xbeef);
    regs.set(RegisterFile::R16::HL, 0x1111);
    t.run(0xdd, 0xe3);
    CHECK(regs.get(RegisterFile::R16::IX) == 0x1234);
    CHECK(regs.get(RegisterFile::R16::HL) == 0x1111);
    CHECK(t.memory.read16(0x9000) == 0xbeef);
    CHECK(t.z80.cycle_count() == 23);
  }

  SECTION("dd 76 is still halt, because neither override row covers it") {
    t.run(0xdd, 0x76);
    CHECK(t.z80.halted());
    CHECK(t.z80.pc() == 1); // halt rewinds so it re-executes; the dd is not re-read
  }
}

} // namespace specbolt::v4
