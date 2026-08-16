#pragma once

// The shapes a parsed instruction table is made of, all of them plain value
// types. `Name`, `Reference` and `Operand` are non-type template parameters
// later, so those three are *structural*: literal, with every member public,
// recursively, which is what `Name` exists to be. The rest hold
// `std::string_view`s into the description and so could not be however they
// were written.

#include "refract/Pattern.hpp"
#include "refract/Vector.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

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
  static constexpr std::size_t capacity = std::tuple_size_v<decltype(storage)>;
  [[nodiscard]] constexpr std::string_view view() const { return {storage.data(), length}; }
  [[nodiscard]] constexpr bool empty() const { return length == 0; }
  constexpr bool operator==(const Name &) const = default;
};

// How a `.cpu` file spells an enumerator, when that differs from what C++ calls
// it. Written as a C++26 annotation (P3394) on the enumerator itself:
//
//   enum class Direction { Up [[=Spelling{"i"}]], Down [[=Spelling{"d"}]] };
//
// The enum is the thing that knows. `Direction::Up` means "step forwards", and
// that the Z80 writes it `i` is a fact about the Z80's assembly syntax, not
// about the direction, so it belongs on the declaration rather than in a
// table the description has to keep in step.
//
// An annotation's type must be *structural*, which is exactly what `Name` was
// built for: `std::string_view` here is rejected outright.
struct Spelling {
  Name text{};
};

// Which vocabulary to look a value up in, and what says which of its members to
// take: a slice of the opcode, or (when `from_view` is set) the decoding
// table's own parameter, which a prefix chose and the instruction does not
// carry. Anything a row can write `{vocabulary:slice}` in holds one.
struct Reference {
  std::uint8_t vocabulary_index{};
  std::uint8_t slice_index{};
  bool from_view{};
  constexpr bool operator==(const Reference &) const = default;
};

// An operand is a constant, a name the CPU can resolve, or a vocabulary
// reference. A name is only a name here, whatever it denotes on the machine: a
// register, a register pair, a single flag bit (the Z80's `a`, `hl` and `carry`
// are one of each). Wrapping one in parentheses says to use it as an address
// rather than as a value, which is orthogonal to all of the above.
struct Operand {
  enum class Kind : std::uint8_t { Constant, Named, Immediate, Vocabulary, Discard };
  Kind kind{};
  Name name{};
  std::uint16_t constant{};
  std::uint8_t width{};
  Reference reference{};
  bool indirect{};
  // The address is this operand offset by a displacement byte the instruction
  // carries, as in the Z80's `(ix+d)`. Forming it is the machine's job because
  // paying for it is.
  bool displaced{};
  std::uint8_t write_back_delay{};
  // The value is in the instruction: these bits of the opcode. A vocabulary of
  // plain numbers needs no code of its own, so choosing between its members is
  // a run-time read rather than one function per member.
  bool from_opcode{};
  BitSlice slice{};
  // The operand says which parameter it feeds rather than relying on where it
  // sits, as `value=(hl)` does in the Z80's description. Empty when the row
  // wrote it positionally, which is almost always. See `operand_for_parameter`
  // in Execute.hpp.
  Name parameter{};
  // Chosen by the table's view, so it cannot be folded away at compile time.
  // `reference.vocabulary_index` says which vocabulary, and the generated code
  // turns that into the list of locations the view indexes.
  bool from_view{};
  // Carried from the vocabulary this came from, because by the time a name is
  // looked up the vocabulary is long gone.
  Name scope{};
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
  static constexpr std::size_t max_arguments = 3;
  // What this member decides about the operation it names, as a call: the row
  // fills the arguments the encoding varies, and these are the rest.
  Vector<Operand, max_arguments> arguments{};
  // The text is an operand, parsed once here rather than per opcode at splice time.
  Operand operand{};
  bool hole{};
  constexpr bool operator==(const Member &) const = default;
};

struct Vocabulary {
  static constexpr std::size_t max_members = 8;
  std::string_view name{};
  // Which scope its members are looked up in, named by the `:` clause of a
  // declaration such as the Z80's `vocab pair : R16 = bc de hl sp`.
  // Empty means the CPU's locations, which is what most of them are. Compared
  // exactly, unlike a member, because it names a C++ type rather than something
  // written the way assembly is written.
  std::string_view scope{};
  Vector<Member, max_members> members{};
  // Where it was declared, so a check that fires elsewhere can point at the line
  // that has to change. A continuation folds to the line the declaration starts
  // on.
  std::size_t line{};
};

// A derived table re-reads its parent's rows with some vocabulary members
// renamed; the Z80's `dd` page is its `base` page read with `pair.hl -> ix`. A
// rule names the vocabulary it rewrites as well as the member, because the same
// text means different things in different vocabularies (there, `reg.h` is
// renamed by a view and the `real.h` of an indexed load is not). The right side
// is a whole member, so a substitute may bring its own operation and its own
// access sequence.
struct Rule {
  std::uint8_t vocabulary_index{};
  std::string_view from{};
  Member to{};
  // The replacement is chosen by the table's view rather than fixed, which is
  // what lets one table stand for every member the view can select: the Z80's
  // `pair.hl -> {index:view}` covers `ix` and `iy` at once.
  bool to_is_view{};
  std::uint8_t to_vocabulary{};
  constexpr bool operator==(const Rule &) const = default;
};

using Rules = Vector<Rule, 6>;

// A vocabulary that *is* its slice: member n is the number n, as in the Z80's
// `bit = 0 1 2 3 4 5 6 7`. Its members differ in a value and nothing else, with
// no operation to splice, no location to name and no addressing mode to pay
// for, so the choice between them need not be baked into a function, because
// the opcode already carries it.
//
// Identity is the load-bearing half, and it is easy to miss. A vocabulary whose
// member is a *function* of the slice rather than the slice itself looks just
// as numeric, and reading the bits would answer with the index instead of the
// value: the Z80's `rst = 0x00 0x08 ... 0x38` would give `rst 3` where
// `rst 0x18` was meant, and its `imode = 0 0 1 2 0 0 1 2` is not even injective.
// Those keep a function each, worth 21 bodies between them in that description,
// which is not worth a lookup table.
[[nodiscard]] constexpr bool is_numeric(const Vocabulary &vocabulary) {
  auto any = false;
  for (const auto [at, member]: std::views::enumerate(vocabulary.members)) {
    if (member.hole)
      continue;
    if (member.operand.kind != Operand::Kind::Constant || !member.operation.empty() || !member.arguments.empty())
      return false;
    if (member.operand.constant != at)
      return false;
    any = true;
  }
  return any;
}

// Where a reference is resolved: the vocabularies to look in, the encoding the
// row matched, the opcode that selects within it, the renaming the table
// applies to what it decodes, and the view a prefix chose.
//
// Taken by const reference throughout. `Rules` holds whole members, so this is
// large enough that copying it per call would be paid for in constant
// evaluation, which is where this file's cost lives.
struct Resolution {
  std::span<const Vocabulary> vocabularies{};
  Pattern matched{};
  Rules rules{};
  std::uint8_t opcode{};
  std::uint8_t view{};
};

// Which rule, if any, rewrites this member of this vocabulary. The two
// functions below must agree about which rule fires, since one returns the
// member it produces and the other where that member came from, so they ask the
// same question rather than each spelling it out.
[[nodiscard]] constexpr const Rule *rule_for(
    const Rules &rules, const Reference reference, const std::string_view display) {
  const auto found = std::ranges::find_if(rules,
      [&](const Rule &rule) { return rule.vocabulary_index == reference.vocabulary_index && rule.from == display; });
  return found == rules.end() ? nullptr : &*found;
}

// The one place a reference is followed, and therefore the one place a derived
// table's renaming has to happen. Every column resolves the same way: the slice
// picks a member, the opcode says which.
// A parameterised table is decoded once per value its view can take without
// being generated once per value, so `view` reaches here alongside the opcode.
// Checks pass the default: every member of a view vocabulary must have the same
// shape, which `check_view_vocabulary` in Parse.hpp requires, so anything a
// check asks is true of all of them or none.
[[nodiscard]] constexpr Member member_of(const Resolution &at, const Reference reference) {
  const auto which = reference.from_view ? at.view : at.matched.slices[reference.slice_index].extract(at.opcode);
  const auto &member = at.vocabularies[reference.vocabulary_index].members[which];
  if (const auto *rule = rule_for(at.rules, reference, member.display))
    return rule->to_is_view ? at.vocabularies[rule->to_vocabulary].members[at.view] : rule->to;
  return member;
}

// Which vocabulary a reference finally lands in, and whether the view chose the
// member. Only `resolve` needs this: an operand the view chose must name the
// vocabulary rather than the member, because the member is not known yet.
[[nodiscard]] constexpr std::pair<std::uint8_t, bool> source_of(const Resolution &at, const Reference reference) {
  // Member 0 stands for all of them here: this matches rules by display text
  // alone, and every member of a view vocabulary shares a shape and so is
  // rewritten by the same rule or by none.
  const std::size_t which = reference.from_view ? 0u : at.matched.slices[reference.slice_index].extract(at.opcode);
  const auto &member = at.vocabularies[reference.vocabulary_index].members[which];
  if (const auto *rule = rule_for(at.rules, reference, member.display))
    return {rule->to_vocabulary, rule->to_is_view};
  return {reference.vocabulary_index, reference.from_view};
}

// A reference operand names whichever vocabulary member its slice selects, and that
// member is written the same way an operand is written in a row.
[[nodiscard]] constexpr Operand resolve(const Resolution &at, const Operand operand) {
  if (operand.kind != Operand::Kind::Vocabulary)
    return operand;
  auto result = member_of(at, operand.reference).operand;
  // The member supplies everything about the operand except which parameter it
  // was written against, which is the row's business and not the vocabulary's.
  result.parameter = operand.parameter;
  result.scope = Name{at.vocabularies[operand.reference.vocabulary_index].scope};
  // A number the opcode already carries: say where, rather than which. Every
  // member of the vocabulary then resolves to the same operand, so the eight
  // functions that differed only in a bit index become one.
  if (!operand.reference.from_view && is_numeric(at.vocabularies[operand.reference.vocabulary_index])) {
    result.from_opcode = true;
    result.slice = at.matched.slices[operand.reference.slice_index];
    result.constant = 0;
    return result;
  }
  // The member supplies the shape (indirect, displaced, what a write-back
  // idles for) but *which* member is not known until the table's view has
  // been chosen, so the vocabulary is carried instead of a name. The generated
  // code turns it into the list of locations the view selects between.
  if (const auto [vocabulary, from_view] = source_of(at, operand.reference); from_view) {
    result.from_view = true;
    result.reference.vocabulary_index = vocabulary;
  }
  return result;
}

inline constexpr std::size_t max_operands = 4;

// One application of one operation, or a transfer into another decoding table.
// A row is an ordered list of these, which is where cost lives: an internal
// delay is a step like any other.
struct Step {
  // `If` applies an operation that yields a bool and abandons the rest of the
  // row when it is false. That is the whole of what a condition can do here:
  // there is no way to guard a step in the middle and resume after it, so a
  // description whose conditionals are not a tail cannot be written. (Every
  // Z80 conditional is, which is why this has been enough.)
  enum class Kind : std::uint8_t { Apply, Goto, If };
  Kind kind{};
  std::uint8_t target{};
  // A goto may name a member of the target's view vocabulary, as the Z80's
  // `goto indexed(ix)` does, or write `view` to hand on the view this table was
  // decoded under.
  std::uint8_t target_view{};
  bool forwards_view{};
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
  // and hands it to the table it goes to. What it is for is an encoding whose
  // opcode byte is not its last, which on the Z80 is `dd cb` and nothing else.
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
  // A table declared `t(view:v)` is decoded once for each member of `v` without
  // being generated once for each; the Z80's is `table indexed(view:index)`.
  // The name is what a row writes where a slice letter would go; empty means
  // the table takes no view.
  std::string_view view_name{};
  std::uint8_t view_vocabulary{};
  [[nodiscard]] constexpr bool takes_view() const { return !view_name.empty(); }
};

// A row that only transfers elsewhere renders nothing and does nothing: it is a
// prefix, and what follows it is the instruction.
[[nodiscard]] constexpr std::optional<std::uint8_t> transfers_to(const Row &row) {
  if (row.steps.size() == 1 && row.steps[0].kind == Step::Kind::Goto)
    return row.steps[0].target;
  return std::nullopt;
}

// Which row, if any, a table decodes each opcode to.
using DecodeTable = std::array<std::optional<std::size_t>, 256>;

// A whole parsed description, as everything downstream of the parse sees it.
// Spans, because the storage belongs to whoever did the parsing: `Table.hpp`
// for the description this build was compiled against, a test for one of its
// own. Holding it as one value is what lets a consumer be handed "the table"
// rather than five of its parts.
struct Description {
  std::span<const Vocabulary> vocabularies;
  std::span<const Row> rows;
  std::span<const TableDecl> tables;
  std::span<const DecodeTable> decoded;
  // Decoding starts here; no name is special.
  std::uint8_t entry{};

  // What this table decodes this opcode to, or nothing. Every table is total in
  // a description that passes its checks, but this is what those checks are
  // written against, so it does not assume it.
  [[nodiscard]] constexpr const Row *row_for(const std::uint8_t table, const std::uint8_t opcode) const {
    const auto index = decoded[table][opcode];
    return index ? &rows[*index] : nullptr;
  }

  // The renaming every row this table decodes is read under: its own rows as
  // well as the ones it inherits.
  [[nodiscard]] constexpr const Rules &rules_for(const std::uint8_t table) const { return tables[table].rules; }
};

} // namespace specbolt::refract
