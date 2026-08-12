#pragma once

// What this library needs from a machine, written down in one place.
//
// A `.cpu` description names operations and locations; this says how the
// framework reaches the thing those names denote, and how it fetches, accesses
// memory, forms an indexed address and spends time. A description plus a type
// satisfying `Machine` is a complete emulator; neither alone is anything.
//
// The functions are found by ordinary unqualified lookup, so a machine supplies
// them as free functions in its own namespace and argument-dependent lookup
// does the rest. Stating the set as a concept means a machine that is missing
// one, or has one with the wrong shape, is told so here rather than through a
// failure deep inside a generated instruction.

#include <concepts>
#include <cstdint>

namespace specbolt::refract {

// The two scopes a description's names are resolved against: the operations it
// may name as verbs, and the locations it may read and write. Both are
// `consteval` and return reflections, which is why they are checked only for
// being callable -- their element type is `std::meta::info`, and requiring that
// here would drag `<meta>` into every consumer.
template<typename M>
concept HasScopes = requires {
  { operation_scopes() };
  { location_scopes() };
};

// Everything the framework does *to* a machine. `delay` is separate from the
// accesses because an idle cycle is not a transfer, and `displaced_address` is
// separate because how a base and an offset combine, and what that costs, is
// the machine's business rather than the format's.
template<typename M>
concept Machine = HasScopes<M> && requires(M &machine, const std::uint16_t address, const std::uint8_t byte) {
  // Reading the instruction stream.
  { fetch_opcode(machine) } -> std::same_as<std::uint8_t>;
  { fetch_immediate(machine, byte) } -> std::convertible_to<std::uint16_t>;

  // Reading and writing memory, in both widths a row can ask for.
  { read_memory(machine, address) } -> std::same_as<std::uint8_t>;
  { read_memory16(machine, address) } -> std::same_as<std::uint16_t>;
  { write_memory(machine, address, byte) };
  { write_memory16(machine, address, address) };

  // Forming an indexed address, told how many bytes were already read inside
  // whatever window the machine spends doing it.
  { displaced_address(machine, address, byte, byte) } -> std::same_as<std::uint16_t>;

  // Spending time on nothing.
  { delay(machine, byte) };
};

} // namespace specbolt::refract
