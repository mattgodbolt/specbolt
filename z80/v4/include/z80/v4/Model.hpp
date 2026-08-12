#pragma once

// The shapes a parsed instruction table is made of. Everything here is a plain
// value type: several are non-type template parameters later, so they are
// *structural* -- literal, with every member public, recursively.

#ifndef SPECBOLT_MODULES
#include "z80/v4/Matched.hpp"
#include "z80/v4/Vector.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#endif

namespace specbolt::v4 {

// Structural, so it can be a template argument. v2 has its own for the same
// reason; this one is v4's.
struct Name {
  std::array<char, 15> storage{};
  std::size_t length{};
  constexpr Name() = default;
  template<std::size_t N>
  constexpr Name(const char (&text)[N]) : Name(std::string_view{text, N - 1}) {} // NOLINT(*-explicit-constructor)
  constexpr Name(const std::string_view text) { // NOLINT(*-explicit-constructor)
    if (text.size() > storage.size())
      throw std::length_error("name does not fit");
    std::ranges::copy(text, storage.begin());
    length = text.size();
  }
  static constexpr std::size_t capacity = decltype(storage){}.size();
  [[nodiscard]] constexpr std::string_view view() const { return {storage.data(), length}; }
  [[nodiscard]] constexpr bool empty() const { return length == 0; }
  constexpr bool operator==(const Name &) const = default;
};

// Which vocabulary to look a value up in, and which slice of the opcode says
// which of its members to take. Anything a row can write `{r:z}` in holds one.
struct Reference {
  std::uint8_t field_index{};
  std::uint8_t slice_index{};
  constexpr bool operator==(const Reference &) const = default;
};

// An operand is a constant, a name the CPU can resolve, or a field reference.
// `a`, `hl`, `carry` and `f` are all just names. Wrapping one in parentheses
// says to use it as an address rather than as a value, which is orthogonal to
// all of the above.
struct Operand {
  enum class Kind : std::uint8_t { Constant, Named, Immediate, Field, Discard };
  Kind kind{};
  Name name{};
  std::uint16_t constant{};
  std::uint8_t width{};
  Reference reference{};
  bool indirect{};
  // `(ix+d)`: the address is this operand offset by a displacement byte, which
  // is the machine's to form because it is the machine's to pay for.
  bool displaced{};
  std::uint8_t write_back_delay{};
  constexpr bool operator==(const Operand &) const = default;
};

// Text with the values it carries taken out of it. A piece is a literal chunk,
// a vocabulary member to look up, a value read from the encoding, or the
// displacement an indexed addressing mode carries.
struct Piece {
  enum class Kind : std::uint8_t { Literal, Field, Imm8, Imm16, Displacement, Relative };
  Kind kind{};
  std::string_view text{};
  Reference reference{};
  constexpr bool operator==(const Piece &) const = default;
};

struct Member {
  static constexpr std::size_t max_pieces = 3;
  std::string_view display{};
  // The display, split around whatever it renders from the instruction: an
  // indexed mode writes its displacement inline, so the disassembler renders
  // rather than parses.
  Vector<Piece, max_pieces> pieces{};
  std::string_view primitive{};
  std::optional<Operand> appended{};
  // The text is an operand, parsed once here rather than per opcode at splice time.
  Operand operand{};
  bool hole{};
  // An addressing mode carries its own access sequence. This one says how long
  // the machine idles between reading through it and writing back.
  std::uint8_t write_back_delay{};
  constexpr bool operator==(const Member &) const = default;
};

struct Field {
  static constexpr std::size_t max_values = 8;
  char name{};
  Vector<Member, max_values> values{};
};

// A derived table re-reads its parent's rows with some vocabulary members
// renamed: `dd` is `base` read with `p.hl -> ix`. A rule names the vocabulary
// it rewrites as well as the member, because the same text means different
// things in different vocabularies -- `r.h` is renamed by a view and the `s.h`
// of an indexed load is not. The right side is a whole member, so a substitute
// may bring its own primitive and its own access sequence.
struct Rule {
  std::uint8_t field_index{};
  std::string_view from{};
  Member to{};
  constexpr bool operator==(const Rule &) const = default;
};

using Rules = Vector<Rule, 6>;

// The one place a reference is followed, and therefore the one place a derived
// table's renaming has to happen. Every column resolves the same way: the slice
// picks a member, the opcode says which.
[[nodiscard]] constexpr Member member_of(const std::span<const Field> fields, const Reference reference,
    const Matched &matched, const std::uint8_t opcode, const Rules &rules = {}) {
  const auto &member = fields[reference.field_index].values[matched.slices[reference.slice_index].extract(opcode)];
  for (const auto &rule: rules)
    if (rule.field_index == reference.field_index && rule.from == member.display)
      return rule.to;
  return member;
}

inline constexpr std::size_t max_operands = 4;

// One application of one primitive, or a transfer into another decoding table.
// A row is an ordered list of these, which is where cost lives: an internal
// delay is a step like any other.
struct Step {
  // `If` applies a primitive that yields a bool and abandons the rest of the
  // row when it is false. Every Z80 conditional puts its conditional half last,
  // so guarding the remainder is all a condition ever has to do.
  enum class Kind : std::uint8_t { Apply, Goto, If };
  Kind kind{};
  std::uint8_t target{};
  std::string_view verb{};
  std::optional<Reference> verb_reference{};
  Vector<Operand, max_operands> destinations{};
  Vector<Operand, max_operands> operands{};
  constexpr bool operator==(const Step &) const = default;
};

struct Row {
  static constexpr std::size_t max_pieces = 12;
  static constexpr std::size_t max_steps = 6;
  Matched matched{};
  std::string_view mnemonic{};
  Vector<Piece, max_pieces> pieces{};
  std::uint8_t immediate_bytes{};
  // `d` in the encoding: this row reads a displacement it does not use itself,
  // and hands it to the table it goes to. Only `dd cb` needs this.
  bool reads_displacement{};
  std::uint8_t table{};
  Vector<Step, max_steps> steps{};
  std::size_t line{};
};

struct TableDecl {
  std::string_view name{};
  std::size_t line{};
  // A derived table decodes its parent's rows under `rules`, and may carry rows
  // of its own that override them.
  bool derived{};
  std::uint8_t parent{};
  Rules rules{};
};

} // namespace specbolt::v4
