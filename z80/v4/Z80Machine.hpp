#pragma once

// How `refract` drives this chip: the functions in its `Machine` concept, and
// nothing else. Fetching, memory in both widths, forming an indexed address,
// and spending time.
//
// They are free functions because that is how the framework finds them: it
// calls them unqualified on a `Cpu &`, and argument-dependent lookup reaches
// this namespace. Nothing here is virtual and nothing is dispatched at run
// time.
//
// Four of the seven carry a real fact about this chip: how wide an immediate
// is, how a displacement combines with a base and what that costs, and in which
// order the two halves of a sixteen-bit access reach the bus. Three --
// `fetch_opcode`, `read_memory`, `write_memory` -- are pure renames of a `Z80`
// method, and they exist only because this namespace already spells `read` and
// `write` as "read a location" (see Locations.hpp). That pressure is an
// artefact of using free functions; if the framework called members, those
// three could be methods on `Z80` and this file would be only the four that say
// something. See the note in NOTES.md.

#include "z80/v4/Locations.hpp"
#include "z80/v4/Operations.hpp"
#include "z80/v4/Z80.hpp"

#include <cstdint>

namespace specbolt::v4 {

[[nodiscard]] inline std::uint8_t fetch_opcode(Cpu &cpu) { return cpu.read_opcode(); }

[[nodiscard]] inline std::uint16_t fetch_immediate(Cpu &cpu, const std::uint8_t width) {
  return width == 1 ? cpu.read_immediate() : cpu.read_immediate16();
}

// How a displacement offsets a base, and what forming that address costs. Both
// are facts about the machine: a 6502 would wrap within page zero for one mode
// and charge for a page crossing in another.
//
// The Z80 sign-extends, and spends a five-T-state window doing it -- but any
// immediate the instruction also carries is read *inside* that window, not
// before it. That is why `ld (ix+d), n` is 19 T-states and not 22, and why the
// framework says how many bytes it already read.
[[nodiscard]] inline std::uint16_t displaced_address(
    Cpu &cpu, const std::uint16_t base, const std::uint8_t offset, const std::uint8_t immediate_bytes) {
  delay(cpu, static_cast<std::uint8_t>(5 - 3 * immediate_bytes));
  return static_cast<std::uint16_t>(base + static_cast<std::int8_t>(offset));
}

[[nodiscard]] inline std::uint8_t read_memory(Cpu &cpu, const std::uint16_t address) { return cpu.read(address); }
inline void write_memory(Cpu &cpu, const std::uint16_t address, const std::uint8_t value) { cpu.write(address, value); }

// Two accesses, low byte first, because that is what the bus sees.
[[nodiscard]] inline std::uint16_t read_memory16(Cpu &cpu, const std::uint16_t address) {
  const auto low = cpu.read(address);
  return static_cast<std::uint16_t>(cpu.read(static_cast<std::uint16_t>(address + 1)) << 8 | low);
}
inline void write_memory16(Cpu &cpu, const std::uint16_t address, const std::uint16_t value) {
  cpu.write(address, static_cast<std::uint8_t>(value));
  cpu.write(static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(value >> 8));
}

} // namespace specbolt::v4
