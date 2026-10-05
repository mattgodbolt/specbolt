#pragma once

// A minimal NMOS 6502, refract's second machine: registers, 64K of memory, a cycle count, and the operations that reach
// the machine. Nothing here is shared with the Z80 but the library; it is here to show that the format describes a
// second, quite different CPU. See z80/v4/notes/6502.md.

#include "refract/Model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace specbolt::m6502 {

// The registers a description may name. `ea` is not a programmer's register: it is the effective address an
// addressing mode forms before the access, which the chip keeps in its own internal latches. Naming it is how an
// indexed or indirect mode is written as steps (see 6502.cpu).
enum class R8 : std::uint8_t { a, x, y, s, p };
enum class R16 : std::uint8_t { pc, ea };

struct Machine {
  std::array<std::uint8_t, 0x10000> memory{};
  std::uint8_t a{};
  std::uint8_t x{};
  std::uint8_t y{};
  std::uint8_t s{0xfd};
  std::uint8_t p{0x24};
  std::uint16_t pc{};
  std::uint16_t ea{};
  std::size_t cycles{};
  std::size_t instructions_left{};

  // What the framework calls. Every bus access costs a cycle.
  bool start_instruction() {
    if (instructions_left == 0)
      return false;
    --instructions_left;
    return true;
  }
  [[nodiscard]] std::uint8_t fetch_opcode() { return read_memory(pc++); }
  [[nodiscard]] std::uint8_t fetch_immediate() { return read_memory(pc++); }
  [[nodiscard]] std::uint8_t read_memory(const std::uint16_t address) {
    ++cycles;
    return memory[address];
  }
  [[nodiscard]] std::uint16_t read_memory16(const std::uint16_t address) {
    const auto low = read_memory(address);
    return static_cast<std::uint16_t>(read_memory(static_cast<std::uint16_t>(address + 1)) << 8 | low);
  }
  void write_memory(const std::uint16_t address, const std::uint8_t value) {
    ++cycles;
    memory[address] = value;
  }
  void write_memory16(const std::uint16_t address, const std::uint16_t value) {
    write_memory(address, static_cast<std::uint8_t>(value));
    write_memory(static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(value >> 8));
  }
  [[nodiscard]][[= refract::location.reads]] std::uint8_t read(const R8 which) const {
    switch (which) {
      case R8::a: return a;
      case R8::x: return x;
      case R8::y: return y;
      case R8::s: return s;
      case R8::p: return p;
    }
    return 0;
  }
  [[= refract::location.writes]] void write(const R8 which, const std::uint8_t value) {
    switch (which) {
      case R8::a: a = value; break;
      case R8::x: x = value; break;
      case R8::y: y = value; break;
      case R8::s: s = value; break;
      case R8::p: p = value; break;
    }
  }
  [[nodiscard]][[= refract::location.reads]] std::uint16_t read(const R16 which) const {
    return which == R16::pc ? pc : ea;
  }
  [[= refract::location.writes]] void write(const R16 which, const std::uint16_t value) {
    (which == R16::pc ? pc : ea) = value;
  }

  // Operations a description may name that need the machine: time, the stack, and the addressing modes whose cost
  // depends on the data.

  // Spends `count` cycles on nothing: the 6502's dummy reads, which do occupy the bus but whose result nobody uses.
  [[= refract::operation]] void delay(const std::uint8_t count) { cycles += count; }

  // `zp,X`: the base plus the index, wrapped within page zero. The addition costs a (dummy read) cycle.
  [[nodiscard]][[= refract::operation]] std::uint16_t zp_index(const std::uint8_t base, const std::uint8_t index) {
    ++cycles;
    return static_cast<std::uint8_t>(base + index);
  }
  // `abs,X`, `abs,Y` and `(zp),Y` for a read: the base plus the index, and one more cycle only if that crosses a page.
  [[nodiscard]][[= refract::operation]] std::uint16_t index(const std::uint16_t base, const std::uint8_t offset) {
    const auto address = static_cast<std::uint16_t>(base + offset);
    if ((address & 0xff00) != (base & 0xff00))
      ++cycles;
    return address;
  }
  // The same for a write or a read-modify-write, which always takes the extra cycle.
  [[nodiscard]][[= refract::operation]] std::uint16_t index_store(const std::uint16_t base, const std::uint8_t offset) {
    ++cycles;
    return static_cast<std::uint16_t>(base + offset);
  }
  // A pointer in page zero: its high byte wraps to the start of the page rather than leaving it.
  [[nodiscard]][[= refract::operation]] std::uint16_t zp_pointer(const std::uint16_t at) {
    const auto low = read_memory(static_cast<std::uint8_t>(at));
    return static_cast<std::uint16_t>(read_memory(static_cast<std::uint8_t>(at + 1)) << 8 | low);
  }
  // `jmp (abs)`: the pointer's high byte comes from the same page, the NMOS chip's famous bug.
  [[nodiscard]][[= refract::operation]] std::uint16_t jmp_pointer(const std::uint16_t at) {
    const auto low = read_memory(at);
    const auto high_at = static_cast<std::uint16_t>((at & 0xff00) | static_cast<std::uint8_t>(at + 1));
    return static_cast<std::uint16_t>(read_memory(high_at) << 8 | low);
  }
  // A taken branch: one cycle for being taken, one more if it lands on another page.
  [[nodiscard]][[= refract::operation]] std::uint16_t branch(const std::uint16_t from, const std::uint8_t offset) {
    ++cycles;
    const auto to = static_cast<std::uint16_t>(from + static_cast<std::int8_t>(offset));
    if ((to & 0xff00) != (from & 0xff00))
      ++cycles;
    return to;
  }

  // The stack is page one, and `s` counts down through it.
  [[= refract::operation]] void push8(const std::uint8_t value) { write_memory(0x0100 | s--, value); }
  [[nodiscard]][[= refract::operation]] std::uint8_t pull8() { return read_memory(0x0100 | ++s); }
  [[= refract::operation]] void push16(const std::uint16_t value) {
    push8(static_cast<std::uint8_t>(value >> 8));
    push8(static_cast<std::uint8_t>(value));
  }
  [[nodiscard]][[= refract::operation]] std::uint16_t pull16() {
    const auto low = pull8();
    return static_cast<std::uint16_t>(pull8() << 8 | low);
  }
  // `php` pushes the status with the break and unused bits set; `plp` and `rti` ignore them on the way back.
  [[= refract::operation]] void push_status(const std::uint8_t status) { push8(status | 0x30); }
  [[nodiscard]][[= refract::operation]] std::uint8_t pull_status() {
    return static_cast<std::uint8_t>((pull8() & ~0x10) | 0x20);
  }
  // `brk` skips the byte after it, pushes where to return to and the status, masks interrupts and takes the vector.
  [[= refract::operation]] void brk() {
    ++pc;
    push16(pc);
    push_status(p);
    p |= 0x04;
    pc = read_memory16(0xfffe);
  }
};

} // namespace specbolt::m6502
