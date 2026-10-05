#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>

#include "m6502/Target.hpp"
#include "refract/Disassemble.hpp"
#include "refract/Execute.hpp"

// The 6502, refract's second machine, described and interpreted in the same binary as the library's other tests and
// built with nothing of the Z80 in reach. Smoke checks, enough to see that the description compiles into a
// disassembler and an interpreter that agree with the chip on a spread of instructions; see z80/v4/notes/6502.md.

namespace specbolt::m6502 {
namespace {

void load(Machine &machine, const std::uint16_t at, const std::initializer_list<std::uint8_t> bytes) {
  auto address = at;
  for (const auto byte: bytes)
    machine.memory[address++] = byte;
}

std::string text_at(const Machine &machine, const std::uint16_t address) {
  return refract::disassemble(Target::Compiled::description(), address, [&](const std::size_t offset) {
    return machine.memory[static_cast<std::uint16_t>(address + offset)];
  }).text;
}

// Runs the one instruction at 0x0600 on a copy of `machine` and returns the cycles it took.
std::size_t cycles_of(Machine machine, const std::initializer_list<std::uint8_t> bytes) {
  machine.pc = 0x0600;
  machine.cycles = 0;
  machine.instructions_left = 1;
  load(machine, 0x0600, bytes);
  refract::Interpreter<Target>::run(machine);
  return machine.cycles;
}

} // namespace

TEST_CASE("The 6502 description disassembles") {
  Machine machine;
  load(machine, 0x0600,
      {0xa9, 0x42, 0xbd, 0x34, 0x12, 0xb1, 0x10, 0x81, 0x20, 0x96, 0x05, 0xbe, 0x00, 0x03, 0x0a, 0xfe, 0x00, 0x02, 0xd0,
          0xfb, 0x20, 0x00, 0x07, 0x18, 0xa8, 0x6c, 0xfc, 0xff, 0x48, 0x02});
  CHECK(text_at(machine, 0x0600) == "lda #0x42");
  CHECK(text_at(machine, 0x0602) == "lda 0x1234,x");
  CHECK(text_at(machine, 0x0605) == "lda (0x10),y");
  CHECK(text_at(machine, 0x0607) == "sta (0x20,x)");
  CHECK(text_at(machine, 0x0609) == "stx 0x05,y");
  CHECK(text_at(machine, 0x060b) == "ldx 0x0300,y");
  CHECK(text_at(machine, 0x060e) == "asl a");
  CHECK(text_at(machine, 0x060f) == "inc 0x0200,x");
  CHECK(text_at(machine, 0x0612) == "bne 0x060f");
  CHECK(text_at(machine, 0x0614) == "jsr 0x0700");
  CHECK(text_at(machine, 0x0617) == "clc");
  CHECK(text_at(machine, 0x0618) == "tay");
  CHECK(text_at(machine, 0x0619) == "jmp (0xfffc)");
  CHECK(text_at(machine, 0x061c) == "pha");
  CHECK(text_at(machine, 0x061d) == "???");
}

TEST_CASE("The 6502 description runs") {
  SECTION("A counted loop and a store") {
    // ldx #5 ; lda #0 ; clc ; loop: adc #3 ; dex ; bne loop ; sta $0200
    Machine machine{.pc = 0x0600, .instructions_left = 3 + 5 * 3 + 1};
    load(machine, 0x0600, {0xa2, 0x05, 0xa9, 0x00, 0x18, 0x69, 0x03, 0xca, 0xd0, 0xfb, 0x8d, 0x00, 0x02});
    refract::Interpreter<Target>::run(machine);
    CHECK(machine.memory[0x0200] == 15);
    CHECK(machine.x == 0);
    CHECK(machine.pc == 0x060d);
  }
  SECTION("A subroutine call and return") {
    // jsr $0700 ; lda #1 / $0700: ldy #9 ; rts
    Machine machine{.pc = 0x0600, .instructions_left = 4};
    load(machine, 0x0600, {0x20, 0x00, 0x07, 0xa9, 0x01});
    load(machine, 0x0700, {0xa0, 0x09, 0x60});
    refract::Interpreter<Target>::run(machine);
    CHECK(machine.y == 9);
    CHECK(machine.a == 1);
    CHECK(machine.s == 0xfd);
    CHECK(machine.pc == 0x0605);
  }
  SECTION("Indirect indexed and indexed indirect addressing") {
    // ldy #2 ; lda ($10),y ; ldx #4 ; sta ($20,x)
    Machine machine{.pc = 0x0600, .instructions_left = 4};
    load(machine, 0x0600, {0xa0, 0x02, 0xb1, 0x10, 0xa2, 0x04, 0x81, 0x20});
    load(machine, 0x0010, {0x00, 0x03});
    load(machine, 0x0024, {0x00, 0x04});
    machine.memory[0x0302] = 0x99;
    refract::Interpreter<Target>::run(machine);
    CHECK(machine.a == 0x99);
    CHECK(machine.memory[0x0400] == 0x99);
  }
}

TEST_CASE("The 6502 description charges the chip's cycles") {
  // Every bus access is a cycle, so these count fetches, reads and writes, and the delays and page crossings the rows
  // and the machine's operations add on top.
  Machine machine;
  CHECK(cycles_of(machine, {0xa9, 0x42}) == 2); // lda #$42
  CHECK(cycles_of(machine, {0xad, 0x34, 0x12}) == 4); // lda $1234
  CHECK(cycles_of(machine, {0xea}) == 2); // nop
  CHECK(cycles_of(machine, {0x20, 0x00, 0x07}) == 6); // jsr $0700
  CHECK(cycles_of(machine, {0x60}) == 6); // rts
  machine.x = 0x01;
  CHECK(cycles_of(machine, {0xbd, 0x34, 0x12}) == 4); // lda $1234,x within the page
  CHECK(cycles_of(machine, {0x9d, 0x34, 0x12}) == 5); // sta $1234,x, which pays for a crossing either way
  CHECK(cycles_of(machine, {0xfe, 0x00, 0x02}) == 7); // inc $0200,x, writing the old value back first
  machine.x = 0xff;
  CHECK(cycles_of(machine, {0xbd, 0x34, 0x12}) == 5); // lda $1234,x into the next page
  machine.p = 0x24; // zero clear, so bne branches
  CHECK(cycles_of(machine, {0xd0, 0x02}) == 3); // bne, taken, within the page
  CHECK(cycles_of(machine, {0xd0, 0x80}) == 4); // bne, taken, back into the previous page
  machine.p = 0x26; // zero set
  CHECK(cycles_of(machine, {0xd0, 0x02}) == 2); // bne, not taken
}

} // namespace specbolt::m6502
