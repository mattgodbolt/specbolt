#pragma once

// What this library needs from a machine, and from the target that names it, written down in one place.
//
// A `.cpu` description names operations and locations; this says how the framework fetches, accesses memory, forms an
// indexed address and spends time. Operation names resolve against the palettes the target lists and the members the
// machine marks with `[[=refract::operation]]`, and location names against the `read` overloads it marks with
// `[[=refract::location]]` (both in Model.hpp).
//
// This is what the *framework* calls, not everything the machine is asked for: an operation is free to use whatever the
// machine offers, and the Z80's use `bus`, `in`, `out` and the register file besides, which are between the description
// and its own chip. The framework calls these as member functions, directly on the machine, and stating the set as a
// concept means a machine that is missing one, or has one with the wrong shape, is told so here rather than through a
// failure deep inside a generated instruction.
//
// The `read` and `write` overloads for a machine's locations cannot be written down here: how many there are, and what
// they take and return, is whatever the machine declares. A row naming a location the machine cannot reach is
// diagnosed where it is spliced instead.

#include <concepts>
#include <cstdint>
#include <meta>
#include <string_view>
#include <vector>

namespace specbolt::refract {

// The name of the overloads a machine marks `[[=refract::location]]`: the generator reads a location by calling it, so
// `location_scopes` in Execute.hpp refuses a mark on anything else. Named here because this is the file stating what a
// machine must provide. `write` needs no such constant: nothing is marked with it, and the generator spells it at the
// calls it splices.
inline constexpr std::string_view read_verb = "read";

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
// bytes were already read inside whatever window it spends forming the address, and states how many that window holds.
// Asked only of a machine whose description has a displaced row, and then against that row's line; a machine without
// one, such as a 6502, need not provide it.
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
