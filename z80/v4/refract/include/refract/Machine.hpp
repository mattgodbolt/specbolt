#pragma once

// What this library needs from a machine, written down in one place.
//
// A `.cpu` description names operations and locations; this says how the
// framework fetches, accesses memory, forms an indexed address and spends time.
//
// Where *operation* names are resolved is a separate question, answered by the
// `operation_scopes()` the target supplies, since it takes no argument and no
// concept can reach it by lookup. Where *location* names are resolved is not a
// question the target answers at all: a location is a thing the machine can
// read, so the pool is its own `read` overloads. See `location_scopes` in
// Execute.hpp.
//
// This is what the *framework* calls, not everything the machine is asked for:
// an operation is free to use whatever the machine offers, and the Z80's use
// `bus`, `in`, `out` and the register file besides. Those are between the
// description and its own chip, and a concept here would only get in the way.
//
// These are member functions, called directly on the machine. Nothing sits
// between the framework and the chip: no adapter, no traits class, no free
// function found by lookup. Stating the set as a concept means a machine that
// is missing one, or has one with the wrong shape, is told so here rather than
// through a failure deep inside a generated instruction.
//
// A machine also needs `read` and `write` overloads for each kind of location
// it declares, but those cannot be written down here: how many there are, and
// what they take and return, is whatever the machine's own location scopes say.
// A row naming a location the machine cannot reach is diagnosed where it is
// spliced instead.

#include <concepts>
#include <cstdint>
#include <string_view>

namespace specbolt::refract {

// `read` is named here because two things depend on the spelling: the concept
// below, and the scan that derives the location scopes from the overloads of
// that name.
inline constexpr std::string_view read_verb = "read";


// Everything the framework does *to* a machine. `delay` is separate from the
// accesses because an idle cycle is not a transfer, and `displaced_address` is
// separate because how a base and an offset combine, and what that costs, is
// the machine's business rather than the format's.
template<typename M>
concept Machine = requires(M &machine, const std::uint16_t address, const std::uint8_t byte) {
  // Reading the instruction stream.
  { machine.fetch_opcode() } -> std::same_as<std::uint8_t>;
  // Between instructions: false ends the run. The machine does whatever it
  // does in between, which on a Z80 is interrupts and the halt idle.
  { machine.start_instruction() } -> std::same_as<bool>;
  { machine.fetch_immediate(byte) } -> std::convertible_to<std::uint16_t>;

  // Reading and writing memory, in both widths a row can ask for.
  { machine.read_memory(address) } -> std::same_as<std::uint8_t>;
  { machine.read_memory16(address) } -> std::same_as<std::uint16_t>;
  { machine.write_memory(address, byte) };
  { machine.write_memory16(address, address) };

  // Forming an indexed address, told how many bytes were already read inside
  // whatever window the machine spends doing it.
  { machine.displaced_address(address, byte, byte) } -> std::same_as<std::uint16_t>;

  // Spending time on nothing.
  { machine.delay(byte) };
};

} // namespace specbolt::refract
