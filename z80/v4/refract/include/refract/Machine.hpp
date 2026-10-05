#pragma once

// What this library needs from a machine, and from the target that names it, written down in one place.
//
// A `.cpu` description names operations and locations; this says how the framework fetches, accesses memory, forms a
// displaced address and spends time. Operation names resolve against the palettes the target lists and the members the
// machine marks with `[[=refract::operation]]`, and location names against the enums its accessors take, the members it
// marks `[[=refract::location.reads]]` and `[[=refract::location.writes]]` (both in Model.hpp).
//
// This is what the *framework* calls, not everything the machine is asked for: an operation is free to use whatever the
// machine offers, and the Z80's use `bus`, `in`, `out` and the register file besides, which are between the description
// and its own chip. The framework calls these as member functions, directly on the machine, and stating the set as a
// concept means a machine that is missing one, or has one with the wrong shape, is told so here rather than through a
// failure deep inside a generated instruction.
//
// The accessors for a machine's locations cannot be written down here: how many there are, what they are called, and
// what they take and return, is whatever the machine declares and marks. The interpreter finds them by their marks, and
// a row reaching a location in a way the machine does not offer is diagnosed against its line.

#include <concepts>
#include <cstdint>
#include <meta>
#include <vector>

namespace specbolt::refract {

// Everything the framework does *to* every machine. `delay` is separate from the accesses because an idle cycle is not
// a transfer. A machine whose description has displaced rows owes `DisplacingMachine` below as well.
template<typename M>
concept MachineLike =
    requires(M &machine, const std::uint16_t address, const std::uint8_t byte, const std::uint16_t word) {
      // Reading the instruction stream.
      { machine.fetch_opcode() } -> std::same_as<std::uint8_t>;
      // Between instructions: false ends the run. What the machine does in between is its own business.
      { machine.start_instruction() } -> std::same_as<bool>;
      // Reading a byte that follows an opcode. A sixteen-bit immediate is two of them, low byte first: that is the
      // format's rule rather than the machine's, so the disassembler, which has no machine to ask, reads it the same
      // way.
      { machine.fetch_immediate() } -> std::same_as<std::uint8_t>;

      // Reading and writing memory, in both widths a row can ask for.
      { machine.read_memory(address) } -> std::same_as<std::uint8_t>;
      { machine.read_memory16(address) } -> std::same_as<std::uint16_t>;
      { machine.write_memory(address, byte) };
      { machine.write_memory16(address, word) };

      // Spending time on nothing.
      { machine.delay(byte) };
    };

// What a machine adds when its description has displaced rows, such as the Z80's `(ix+d)`: how a base and a signed
// offset combine, and what that costs, which is the machine's business rather than the format's. It is told how many
// instruction bytes were fetched after the displacement and before it was asked, so a machine whose address arithmetic
// overlaps those fetches can charge only what is left; `displacement_window_bytes` is the most it accounts for, and a
// row that fetches more is refused. Asked only of a machine whose description has a displaced row, and then against
// that row's line; a machine without one, such as a 6502, need not provide it.
template<typename M>
concept DisplacingMachine =
    requires(M &machine, const std::uint16_t base, const std::int8_t offset, const std::uint8_t bytes_read) {
      { machine.displaced_address(base, offset, bytes_read) } -> std::same_as<std::uint16_t>;
      { M::displacement_window_bytes } -> std::convertible_to<std::uint8_t>;
    };

// What `Interpreter` is given: the machine, the compiled description it runs, and the palettes the description may draw
// operations from. The machine's own operations are not listed here; it publishes them with `[[=refract::operation]]`,
// and the interpreter finds them.
template<typename T>
concept TargetLike = requires {
  typename T::Machine;
  typename T::Compiled;
  { T::palettes() } -> std::same_as<std::vector<std::meta::info>>;
} && MachineLike<typename T::Machine>;

} // namespace specbolt::refract
