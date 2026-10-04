#pragma once

// What this library needs from a machine, and from the target that names it, written down in one place.
//
// A `.cpu` description names operations and locations; this says how the framework fetches, accesses memory, forms an
// indexed address and spends time. Operation names resolve against the palettes the target lists and the members the
// machine marks with `[[=refract::operation]]` (see Execute.hpp); location names resolve as `read_verb` below says.
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

// The member name that makes a type a location: a public `read(E)` overload on the machine publishes every enumerator
// of `E` to descriptions. Named here because the scan for location scopes (`location_scopes` in Execute.hpp) finds
// overloads by this spelling, and this is the file stating what a machine must provide. `write` needs no such constant:
// the generator spells it at the calls it splices.
inline constexpr std::string_view read_verb = "read";

// Everything the framework does *to* a machine. `delay` is separate from the accesses because an idle cycle is not a
// transfer, and `displaced_address` is separate because how a base and an offset combine, and what that costs, is the
// machine's business rather than the format's.
template<typename M>
concept MachineLike =
    requires(M &machine, const std::uint16_t address, const std::uint8_t byte, const std::uint16_t word) {
      // Reading the instruction stream.
      { machine.fetch_opcode() } -> std::same_as<std::uint8_t>;
      // Between instructions: false ends the run. What the machine does in between is its own business.
      { machine.start_instruction() } -> std::same_as<bool>;
      // Reading the bytes that follow an opcode, in the two widths a row can ask for. Exact rather than convertible: a
      // machine answering the wide one with a narrow type would drop the high byte of every sixteen-bit immediate.
      { machine.fetch_immediate() } -> std::same_as<std::uint8_t>;
      { machine.fetch_immediate16() } -> std::same_as<std::uint16_t>;

      // Reading and writing memory, in both widths a row can ask for.
      { machine.read_memory(address) } -> std::same_as<std::uint8_t>;
      { machine.read_memory16(address) } -> std::same_as<std::uint16_t>;
      { machine.write_memory(address, byte) };
      { machine.write_memory16(address, word) };

      // Forming an indexed address, told how many bytes were already read inside whatever window the machine spends
      // doing it. How many bytes that window holds is stated alongside, and the interpreter checks every displaced row
      // against it.
      { machine.displaced_address(address, byte, byte) } -> std::same_as<std::uint16_t>;
      { M::displacement_window_bytes } -> std::convertible_to<std::uint8_t>;

      // Spending time on nothing.
      { machine.delay(byte) };
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
