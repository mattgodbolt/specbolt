#pragma once

// The shapes a parsed instruction table is made of. Everything here is a plain
// value type: several are non-type template parameters later, so they are
// *structural* -- literal, with every member public, recursively.

#include "refract/Pattern.hpp"
#include "refract/Vector.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

namespace specbolt::refract {

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
  std::uint8_t vocabulary_index{};
  std::uint8_t slice_index{};
  constexpr bool operator==(const Reference &) const = default;
};

// An operand is a constant, a name the CPU can resolve, or a field reference.
// `a`, `hl` and `carry` are all just names, whatever they denote on the
// machine -- a register, a register pair, a single flag bit. Wrapping one in parentheses
// says to use it as an address rather than as a value, which is orthogonal to
// all of the above.
struct Operand {
  enum class Kind : std::uint8_t { Constant, Named, Immediate, Vocabulary, Discard };
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
  enum class Kind : std::uint8_t { Literal, Vocabulary, Imm8, Imm16, Displacement, Relative };
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
  std::string_view operation{};
  std::optional<Operand> appended{};
  // The text is an operand, parsed once here rather than per opcode at splice time.
  Operand operand{};
  bool hole{};
  constexpr bool operator==(const Member &) const = default;
};

struct Vocabulary {
  static constexpr std::size_t max_members = 8;
  std::string_view name{};
  Vector<Member, max_members> members{};
};

// A derived table re-reads its parent's rows with some vocabulary members
// renamed: `dd` is `base` read with `p.hl -> ix`. A rule names the vocabulary
// it rewrites as well as the member, because the same text means different
// things in different vocabularies -- `r.h` is renamed by a view and the `s.h`
// of an indexed load is not. The right side is a whole member, so a substitute
// may bring its own operation and its own access sequence.
struct Rule {
  std::uint8_t vocabulary_index{};
  std::string_view from{};
  Member to{};
  constexpr bool operator==(const Rule &) const = default;
};

using Rules = Vector<Rule, 6>;

// The one place a reference is followed, and therefore the one place a derived
// table's renaming has to happen. Every column resolves the same way: the slice
// picks a member, the opcode says which.
[[nodiscard]] constexpr Member member_of(const std::span<const Vocabulary> vocabularies, const Reference reference,
    const Pattern &matched, const std::uint8_t opcode, const Rules &rules = {}) {
  const auto &member =
      vocabularies[reference.vocabulary_index].members[matched.slices[reference.slice_index].extract(opcode)];
  for (const auto &rule: rules)
    if (rule.vocabulary_index == reference.vocabulary_index && rule.from == member.display)
      return rule.to;
  return member;
}

// A field operand names whichever vocabulary member its slice selects, and that
// member is written the same way an operand is written in a row.
[[nodiscard]] constexpr Operand resolve(const std::span<const Vocabulary> vocabularies, const Operand operand,
    const Pattern &matched, const std::uint8_t opcode, const Rules &rules = {}) {
  if (operand.kind != Operand::Kind::Vocabulary)
    return operand;
  return member_of(vocabularies, operand.reference, matched, opcode, rules).operand;
}

inline constexpr std::size_t max_operands = 4;

// One application of one operation, or a transfer into another decoding table.
// A row is an ordered list of these, which is where cost lives: an internal
// delay is a step like any other.
struct Step {
  // `If` applies a operation that yields a bool and abandons the rest of the
  // row when it is false. Every Z80 conditional puts its conditional half last,
  // so guarding the remainder is all a condition ever has to do.
  enum class Kind : std::uint8_t { Apply, Goto, If };
  Kind kind{};
  std::uint8_t target{};
  std::string_view operation{};
  std::optional<Reference> operation_reference{};
  Vector<Operand, max_operands> destinations{};
  Vector<Operand, max_operands> operands{};
  constexpr bool operator==(const Step &) const = default;
};

struct Row {
  static constexpr std::size_t max_pieces = 12;
  static constexpr std::size_t max_steps = 6;
  Pattern matched{};
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

} // namespace specbolt::refract
