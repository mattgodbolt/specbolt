#pragma once

// What this library needs from a machine, and from the target that names it,
// written down in one place.
//
// A `.cpu` description names operations and locations; this says how the
// framework fetches, accesses memory, forms an indexed address and spends time.
//
// Where *operation* names are resolved is a separate question, answered by the
// palettes the target lists and by the members the machine publishes with
// `[[=refract::operation]]`; see Execute.hpp. Where *location* names are resolved is not a
// question the target answers at all: a location is a thing the machine can
// read, so the pool is its own `read` overloads. See `location_scopes` in
// Execute.hpp.
//
// This is what the *framework* calls, not everything the machine is asked for:
// an operation is free to use whatever the machine offers, and the Z80's use
// `bus`, `in`, `out` and the register file besides, which are between the
// description and its own chip. The framework calls these as member functions,
// directly on the machine, and stating the set as a concept means a machine
// that is missing one, or has one with the wrong shape, is told so here rather
// than through a failure deep inside a generated instruction.
//
// A machine also needs `read` and `write` overloads for each kind of location
// it declares, but those cannot be written down here: how many there are, and
// what they take and return, is whatever the machine's own location scopes say.
// A row naming a location the machine cannot reach is diagnosed where it is
// spliced instead.

#include <concepts>
#include <cstdint>
#include <meta>
#include <string_view>
#include <vector>

namespace specbolt::refract {

// The name of the member the scan for a machine's location scopes looks for.
// It is named here because that scan finds overloads by this spelling, and
// this is the file stating what a machine must provide: a public `read(E)`
// overload publishes every enumerator of `E` to descriptions. `write` needs no
// such constant: the generator spells it at the calls it splices.
inline constexpr std::string_view read_verb = "read";

// Everything the framework does *to* a machine. `delay` is separate from the
// accesses because an idle cycle is not a transfer, and `displaced_address` is
// separate because how a base and an offset combine, and what that costs, is
// the machine's business rather than the format's.
template<typename M>
concept MachineLike = requires(M &machine, const std::uint16_t address, const std::uint8_t byte) {
  // Reading the instruction stream.
  { machine.fetch_opcode() } -> std::same_as<std::uint8_t>;
  // Between instructions: false ends the run. What the machine does in between
  // is its own business.
  { machine.start_instruction() } -> std::same_as<bool>;
  // Reading the bytes that follow an opcode, in the two widths a row can ask
  // for. Exact rather than convertible: a machine answering the wide one with
  // a narrow type would drop the high byte of every sixteen-bit immediate.
  { machine.fetch_immediate() } -> std::same_as<std::uint8_t>;
  { machine.fetch_immediate16() } -> std::same_as<std::uint16_t>;

  // Reading and writing memory, in both widths a row can ask for.
  { machine.read_memory(address) } -> std::same_as<std::uint8_t>;
  { machine.read_memory16(address) } -> std::same_as<std::uint16_t>;
  { machine.write_memory(address, byte) };
  { machine.write_memory16(address, address) };

  // Forming an indexed address, told how many bytes were already read inside
  // whatever window the machine spends doing it. The count is a template
  // argument so that the machine can refuse, at compile time, a count its
  // window cannot hold.
  { machine.template displaced_address<0>(address, byte) } -> std::same_as<std::uint16_t>;

  // Spending time on nothing.
  { machine.delay(byte) };
};

// What `Interpreter` is given: the machine, the compiled description it runs,
// and the palettes the description may draw operations from. The machine's
// own operations are not listed here; it publishes them with
// `[[=refract::operation]]`, and the interpreter finds them. Checked as a
// constraint so that a target missing a piece is told at the point it is
// named, not inside a generated instruction.
template<typename T>
concept TargetLike = requires {
  typename T::Machine;
  typename T::Compiled;
  { T::palettes() } -> std::same_as<std::vector<std::meta::info>>;
} && MachineLike<typename T::Machine>;

} // namespace specbolt::refract
