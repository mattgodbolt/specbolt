#pragma once

// What a row's verbs mean: every operation a description of this chip may name,
// and where reflection is to look for them.
//
// These are the semantics the table cannot express -- the block moves, the
// exchanges, the flag minutiae. The easy majority of the instruction set became
// rows; this is the awkward remainder, and it is meant to be read as such.

#include "z80/common/Alu.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/v4/Z80.hpp"

#include <array>
#include <cstdint>
#include <meta>

namespace specbolt::v4 {

using Cpu = Z80;

struct Operations {
private:
  // What every block operation does to the flags it does not otherwise touch:
  // parity stands in for "bc has not run out", and flags 3 and 5 come from a
  // value the instruction happens to have to hand, swapped over.
  [[nodiscard]] static Flags counted(const Flags flags, const std::uint16_t bc, const std::uint8_t noise) {
    auto result =
        flags & ~(Flags::Subtract() | Flags::HalfCarry() | Flags::Overflow() | Flags::Flag3() | Flags::Flag5());
    if (bc != 1)
      result = result | Flags::Overflow();
    if (noise & 0x08)
      result = result | Flags::Flag3();
    if (noise & 0x02)
      result = result | Flags::Flag5();
    return result;
  }
  // The in and out block forms count b rather than bc. Their real parity comes
  // from `(value + ((c ± 1) & 0xff)) & 7` exclusive-ored with b, and their half
  // carry and carry from whether that sum passed 255; none of that is modelled,
  // so only sign, zero and flags 3 and 5 are trustworthy here.
  [[nodiscard]] static Flags stepped(Cpu &cpu, const Flags flags) {
    const auto b = static_cast<std::uint8_t>(cpu.get(RegisterFile::R8::B) - 1);
    cpu.set(RegisterFile::R8::B, b);
    return Alu::parity_flags_for(b) | Flags::Subtract() | (flags & Flags::Carry());
  }
  [[nodiscard]] static Alu::R8 nibble(Cpu &cpu, const std::uint8_t value, const Flags flags, const bool right) {
    const auto a = cpu.get(RegisterFile::R8::A);
    const auto updated =
        static_cast<std::uint8_t>(right ? (a & 0xf0) | (value & 0x0f) : (a & 0xf0) | (value >> 4 & 0x0f));
    const auto written = static_cast<std::uint8_t>(right ? value >> 4 | (a & 0x0f) << 4 : value << 4 | (a & 0x0f));
    cpu.delay(4);
    cpu.set(RegisterFile::R8::A, updated);
    return {written, (flags & Flags::Carry()) | Alu::parity_flags_for(updated)};
  }

public:
  static void nop() {}
  static std::uint16_t ld16(const std::uint16_t value) { return value; }
  static std::uint16_t inc16(const std::uint16_t value) { return static_cast<std::uint16_t>(value + 1); }
  static std::uint16_t dec16(const std::uint16_t value) { return static_cast<std::uint16_t>(value - 1); }
  static std::uint8_t ld8(const std::uint8_t value) { return value; }
  static void delay(Cpu &cpu, const std::uint8_t cycles) { cpu.delay(cycles); }
  // Alu::bit takes a mask; the encoding carries an index, as res and set do.
  // Flags 3 and 5 come from whatever was last on the bus, which the row names.
  static Flags bit8(const std::uint8_t value, const std::uint8_t bit, const Flags flags, const std::uint8_t bus) {
    return Alu::bit(value, static_cast<std::uint8_t>(1u << bit), flags, bus);
  }
  static std::uint8_t res8(const std::uint8_t value, const std::uint8_t bit) {
    return static_cast<std::uint8_t>(value & ~(1u << bit));
  }
  static std::uint8_t set8(const std::uint8_t value, const std::uint8_t bit) {
    return static_cast<std::uint8_t>(value | 1u << bit);
  }

  // The rotate and shift family. `Alu` spells the direction as an argument;
  // a table names operations, so each direction gets a name.
  static Alu::R8 rlc8(const std::uint8_t v) { return Alu::rotate_circular8(v, Alu::Direction::Left); }
  static Alu::R8 rrc8(const std::uint8_t v) { return Alu::rotate_circular8(v, Alu::Direction::Right); }
  static Alu::R8 rl8(const std::uint8_t v, const bool carry) { return Alu::rotate8(v, Alu::Direction::Left, carry); }
  static Alu::R8 rr8(const std::uint8_t v, const bool carry) { return Alu::rotate8(v, Alu::Direction::Right, carry); }
  static Alu::R8 sla8(const std::uint8_t v) { return Alu::shift_arithmetic8(v, Alu::Direction::Left); }
  static Alu::R8 sra8(const std::uint8_t v) { return Alu::shift_arithmetic8(v, Alu::Direction::Right); }
  // Undocumented, and shifts a one in where sla shifts a zero.
  static Alu::R8 sll8(const std::uint8_t v) { return Alu::shift_logical8(v, Alu::Direction::Left); }
  static Alu::R8 srl8(const std::uint8_t v) { return Alu::shift_logical8(v, Alu::Direction::Right); }

  // The accumulator forms keep sign, zero and parity rather than recomputing
  // them, which is the whole difference between `rlca` and `rlc a`.
  static Alu::R8 rlca8(const std::uint8_t v, const Flags f) {
    return Alu::fast_rotate_circular8(v, Alu::Direction::Left, f);
  }
  static Alu::R8 rrca8(const std::uint8_t v, const Flags f) {
    return Alu::fast_rotate_circular8(v, Alu::Direction::Right, f);
  }
  static Alu::R8 rla8(const std::uint8_t v, const Flags f) { return Alu::fast_rotate8(v, Alu::Direction::Left, f); }
  static Alu::R8 rra8(const std::uint8_t v, const Flags f) { return Alu::fast_rotate8(v, Alu::Direction::Right, f); }

  // Conditions. A vocabulary member binds one of these and appends the flag it
  // asks about, exactly as `adc` binds `add8` and appends the carry.
  static bool is_set(const bool flag) { return flag; }
  static bool is_clear(const bool flag) { return !flag; }
  static bool nonzero(const std::uint8_t value) { return value != 0; }

  // `djnz` counts without touching the flags, which `dec8` would.
  static std::uint8_t dec8_quiet(const std::uint8_t value) { return static_cast<std::uint8_t>(value - 1); }

  // A relative jump is measured from the byte after the offset, which is where
  // the program counter already is.
  static std::uint16_t relative(const std::uint16_t pc, const std::uint8_t offset) {
    return static_cast<std::uint16_t>(pc + static_cast<std::int8_t>(offset));
  }

  // The port is sixteen bits wide even when the encoding writes eight: the Z80
  // puts the accumulator on the top half.
  static void out_n(Cpu &cpu, const std::uint8_t port, const std::uint8_t value) {
    const auto address = static_cast<std::uint16_t>(value << 8 | port);
    cpu.bus(Bus::io_write, address);
    cpu.out(address, value);
  }
  static std::uint8_t in_n(Cpu &cpu, const std::uint8_t port, const std::uint8_t high) {
    const auto address = static_cast<std::uint16_t>(high << 8 | port);
    cpu.bus(Bus::io_read, address);
    return cpu.in(address);
  }

  // `ex (sp), ix` and `ex (sp), iy` are the same sequence: the row names the
  // register as a literal destination, and a view rewrites vocabularies rather
  // than literals, so each spelling needs its own name here.
  static std::uint16_t ex_sp_ix(Cpu &cpu, const std::uint16_t value) { return ex_sp_hl(cpu, value); }
  static std::uint16_t ex_sp_iy(Cpu &cpu, const std::uint16_t value) { return ex_sp_hl(cpu, value); }

  // Three accesses and two idle stretches, none of which an operand can spell.
  static std::uint16_t ex_sp_hl(Cpu &cpu, const std::uint16_t value) {
    const auto sp = cpu.get(RegisterFile::R16::SP);
    const auto low = cpu.read_memory(sp);
    const auto high = cpu.read_memory(static_cast<std::uint16_t>(sp + 1));
    cpu.delay(1);
    cpu.write_memory(static_cast<std::uint16_t>(sp + 1), static_cast<std::uint8_t>(value >> 8));
    cpu.write_memory(sp, static_cast<std::uint8_t>(value));
    cpu.delay(2);
    return static_cast<std::uint16_t>(high << 8 | low);
  }

  // `in r,(c)` addresses with the whole of bc and sets flags; `out (c),r` does
  // not. Neither is expressible as an operand, because the port space is not
  // memory.
  // Sign, zero and parity come from the byte; the carry is explicitly *not*
  // affected, so it has to be carried through rather than recomputed.
  static Alu::R8 in_c(Cpu &cpu, const std::uint16_t port, const Flags flags) {
    cpu.bus(Bus::io_read, port);
    const auto value = cpu.in(port);
    return {value, Alu::parity_flags_for(value) | (flags & Flags::Carry())};
  }
  static void out_c(Cpu &cpu, const std::uint16_t port, const std::uint8_t value) {
    cpu.bus(Bus::io_write, port);
    cpu.out(port, value);
  }

  // `ld a,i` and `ld a,r` report iff2 in the parity flag, which is the one way
  // a program can see the interrupt state.
  static Alu::R8 ld_a_special(Cpu &cpu, const std::uint8_t value, const Flags flags) {
    return {value, Alu::iff2_flags_for(value, flags, cpu.iff2())};
  }

  // `neg` is `0 - a`, which sub8 already is.
  static Alu::R8 neg8(const std::uint8_t value) { return Alu::sub8(0, value, false); }

  static bool nonzero16(const std::uint16_t value) { return value != 0; }

  // `rrd` and `rld` move a nibble between the accumulator and memory, so both
  // ends change at once and only one of them can be a destination.
  static Alu::R8 rrd8(Cpu &cpu, const std::uint8_t value, const Flags flags) { return nibble(cpu, value, flags, true); }
  static Alu::R8 rld8(Cpu &cpu, const std::uint8_t value, const Flags flags) {
    return nibble(cpu, value, flags, false);
  }

  // The block operations move or compare one byte, step hl (and de), and count
  // bc down. The repeating forms are the same row with a condition and a
  // rewind: the chip really does re-execute the opcode, which is why an
  // interrupt can land in the middle of an `ldir`.
  static Flags block_load(Cpu &cpu, const bool increment, const Flags flags) {
    const auto step = static_cast<std::uint16_t>(increment ? 1 : 0xffff);
    const auto hl = cpu.get(RegisterFile::R16::HL);
    const auto de = cpu.get(RegisterFile::R16::DE);
    const auto bc = cpu.get(RegisterFile::R16::BC);
    const auto byte = cpu.read_memory(hl);
    cpu.write_memory(de, byte);
    cpu.delay(2);
    cpu.set(RegisterFile::R16::HL, static_cast<std::uint16_t>(hl + step));
    cpu.set(RegisterFile::R16::DE, static_cast<std::uint16_t>(de + step));
    cpu.set(RegisterFile::R16::BC, static_cast<std::uint16_t>(bc - 1));
    // Flags 3 and 5 come from the byte plus the accumulator, and swapped over.
    return counted(flags, bc, static_cast<std::uint8_t>(byte + cpu.get(RegisterFile::R8::A)));
  }
  static Flags block_compare(Cpu &cpu, const bool increment, const Flags flags) {
    const auto step = static_cast<std::uint16_t>(increment ? 1 : 0xffff);
    const auto hl = cpu.get(RegisterFile::R16::HL);
    const auto bc = cpu.get(RegisterFile::R16::BC);
    const auto byte = cpu.read_memory(hl);
    cpu.delay(5);
    cpu.set(RegisterFile::R16::HL, static_cast<std::uint16_t>(hl + step));
    cpu.set(RegisterFile::R16::BC, static_cast<std::uint16_t>(bc - 1));
    const auto compared = Alu::sub8(cpu.get(RegisterFile::R8::A), byte, false);
    // Flags 3 and 5 come from the difference, less one where it borrowed.
    const auto noise = static_cast<std::uint8_t>(compared.flags.half_carry() ? compared.result - 1 : compared.result);
    // The comparison's own sign, zero, half-carry and subtract go on last: the
    // count clears two of them, and a compare is entitled to say otherwise.
    constexpr auto compared_flags = Flags::HalfCarry() | Flags::Zero() | Flags::Sign() | Flags::Subtract();
    return (counted(flags, bc, noise) & ~compared_flags) | (compared.flags & compared_flags);
  }
  static Flags block_in(Cpu &cpu, const bool increment, const Flags flags) {
    cpu.delay(1);
    const auto port = cpu.get(RegisterFile::R16::BC);
    cpu.bus(Bus::io_read, port);
    const auto value = cpu.in(port);
    const auto hl = cpu.get(RegisterFile::R16::HL);
    cpu.write_memory(hl, value);
    cpu.set(RegisterFile::R16::HL, static_cast<std::uint16_t>(hl + (increment ? 1 : 0xffff)));
    return stepped(cpu, flags);
  }
  static Flags block_out(Cpu &cpu, const bool increment, const Flags flags) {
    cpu.delay(1);
    const auto hl = cpu.get(RegisterFile::R16::HL);
    const auto value = cpu.read_memory(hl);
    cpu.set(RegisterFile::R16::HL, static_cast<std::uint16_t>(hl + (increment ? 1 : 0xffff)));
    // B is counted down before the port goes on the bus, so it addresses with
    // the new value.
    const auto result = stepped(cpu, flags);
    const auto port = cpu.get(RegisterFile::R16::BC);
    cpu.bus(Bus::io_write, port);
    cpu.out(port, value);
    return result;
  }

  // The exchanges move whole register pairs about, which no operand can name.
  static void exx(Cpu &cpu) { cpu.regs().exx(); }
  static void ex_de_hl(Cpu &cpu) { cpu.regs().ex(RegisterFile::R16::DE, RegisterFile::R16::HL); }
  static void ex_af(Cpu &cpu) { cpu.regs().ex(RegisterFile::R16::AF, RegisterFile::R16::AF_); }
};

// Where the table may name operations from.
[[nodiscard]] consteval std::array<std::meta::info, 2> operation_scopes() { return {^^Operations, ^^Alu}; }

} // namespace specbolt::v4
