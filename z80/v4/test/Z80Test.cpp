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

} // namespace specbolt::v4
