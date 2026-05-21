#include "peripherals/Memory.hpp"
#include "z80/common/Scheduler.hpp"
#include "z80/v4/Z80.hpp"

#include <catch2/catch_test_macros.hpp>

namespace specbolt::v4 {

namespace {

struct Harness {
  Scheduler scheduler;
  Memory memory{4};
  Z80 cpu{scheduler, memory};

  void load(std::uint16_t at, std::initializer_list<std::uint8_t> bytes) {
    std::uint16_t addr = at;
    for (auto b : bytes) memory.write(addr++, b);
    cpu.regs().pc(at);
  }
};

} // namespace

TEST_CASE("v4 dispatch: nop") {
  Harness h;
  h.load(0x8000, {0x00});
  h.cpu.execute_one();
  CHECK(h.cpu.regs().pc() == 0x8001);
  CHECK_FALSE(h.cpu.halted());
}

TEST_CASE("v4 dispatch: halt") {
  Harness h;
  h.load(0x8000, {0x76});
  h.cpu.execute_one();
  CHECK(h.cpu.halted());
}

TEST_CASE("v4 dispatch: ei / di toggle iff1/iff2") {
  Harness h;
  h.load(0x8000, {0xfb, 0xf3});
  h.cpu.execute_one(); // EI
  CHECK(h.cpu.iff1());
  CHECK(h.cpu.iff2());
  h.cpu.execute_one(); // DI
  CHECK_FALSE(h.cpu.iff1());
  CHECK_FALSE(h.cpu.iff2());
}

TEST_CASE("v4 dispatch: jp $nnnn") {
  Harness h;
  h.load(0x8000, {0xc3, 0x34, 0x12});
  h.cpu.execute_one();
  CHECK(h.cpu.regs().pc() == 0x1234);
}

// All 63 `ld r, r'` opcodes come from one emit_ld_rr() shape. Spot-check a
// few representatives (reg→reg, reg→(hl), (hl)→reg) and make sure HALT
// (0x76, which lives in the same opcode range) is still HALT.

TEST_CASE("v4 LdRR: ld b, c (0x41)") {
  Harness h;
  h.cpu.regs().set(RegisterFile::R8::C, 0x42);
  h.load(0x8000, {0x41});
  h.cpu.execute_one();
  CHECK(h.cpu.regs().get(RegisterFile::R8::B) == 0x42);
}

TEST_CASE("v4 LdRR: ld h, a (0x67)") {
  Harness h;
  h.cpu.regs().set(RegisterFile::R8::A, 0xab);
  h.load(0x8000, {0x67});
  h.cpu.execute_one();
  CHECK(h.cpu.regs().get(RegisterFile::R8::H) == 0xab);
}

TEST_CASE("v4 LdRR: ld a, l (0x7d)") {
  Harness h;
  h.cpu.regs().set(RegisterFile::R8::L, 0x5a);
  h.load(0x8000, {0x7d});
  h.cpu.execute_one();
  CHECK(h.cpu.regs().get(RegisterFile::R8::A) == 0x5a);
}

TEST_CASE("v4 LdRR: ld b, (hl) (0x46)") {
  Harness h;
  h.cpu.regs().set(RegisterFile::R16::HL, 0x9000);
  h.memory.write(0x9000, 0xcc);
  h.load(0x8000, {0x46});
  h.cpu.execute_one();
  CHECK(h.cpu.regs().get(RegisterFile::R8::B) == 0xcc);
}

TEST_CASE("v4 LdRR: ld (hl), a (0x77)") {
  Harness h;
  h.cpu.regs().set(RegisterFile::R16::HL, 0x9100);
  h.cpu.regs().set(RegisterFile::R8::A, 0xee);
  h.load(0x8000, {0x77});
  h.cpu.execute_one();
  CHECK(h.memory.read(0x9100) == 0xee);
}

TEST_CASE("v4 LdRR: 0x76 is still HALT, not ld (hl), (hl)") {
  Harness h;
  h.load(0x8000, {0x76});
  h.cpu.execute_one();
  CHECK(h.cpu.halted());
}

// DD/FD prefixed forms share the LdRR generator but with ix_regs/iy_regs.

TEST_CASE("v4 LdRR: ld ixh, b (DD 60)") {
  Harness h;
  h.cpu.regs().set(RegisterFile::R8::B, 0xab);
  h.load(0x8000, {0xdd, 0x60});
  h.cpu.execute_one();
  CHECK(h.cpu.regs().get(RegisterFile::R8::IXH) == 0xab);
}

TEST_CASE("v4 LdRR: ld b, (ix+d) (DD 46 d)") {
  Harness h;
  h.cpu.regs().set(RegisterFile::R16::IX, 0x9000);
  h.memory.write(0x9005, 0x5a); // value at IX+5
  h.load(0x8000, {0xdd, 0x46, 0x05});
  h.cpu.execute_one();
  CHECK(h.cpu.regs().get(RegisterFile::R8::B) == 0x5a);
}

TEST_CASE("v4 LdRR: ld (iy+d), c (FD 71 d) with negative displacement") {
  Harness h;
  h.cpu.regs().set(RegisterFile::R16::IY, 0x9100);
  h.cpu.regs().set(RegisterFile::R8::C, 0xee);
  h.load(0x8000, {0xfd, 0x71, 0xff}); // d = -1
  h.cpu.execute_one();
  CHECK(h.memory.read(0x90ff) == 0xee);
}

TEST_CASE("v4 LdRR: under DD, h/l are remapped (ld ixl, ixh = DD 6c)") {
  Harness h;
  h.cpu.regs().set(RegisterFile::R8::IXH, 0x12);
  h.load(0x8000, {0xdd, 0x6c});
  h.cpu.execute_one();
  CHECK(h.cpu.regs().get(RegisterFile::R8::IXL) == 0x12);
}

} // namespace specbolt::v4
