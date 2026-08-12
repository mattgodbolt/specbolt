#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#ifdef SPECBOLT_MODULES
import z80_v4;
#else
#include "z80/v4/Z80.hpp"
#endif

#ifdef SPECBOLT_MODULES
import peripherals;
#else
#include "peripherals/Memory.hpp"
#include "z80/common/Scheduler.hpp"
#endif

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

  SECTION("dd 76 is still halt, because neither override row covers it") {
    t.run(0xdd, 0x76);
    CHECK(t.z80.halted());
    CHECK(t.z80.pc() == 1); // halt rewinds so it re-executes; the dd is not re-read
  }
}

} // namespace specbolt::v4
