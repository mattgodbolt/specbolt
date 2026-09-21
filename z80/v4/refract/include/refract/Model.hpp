#pragma once

// The shapes a parsed instruction table is made of, all of them plain value
// types. `Name` and `Resolved` are non-type template parameters later, so they
// are *structural*: literal, with every member public, recursively, which is
// what `Name` exists to be. The rest hold `std::string_view`s into the
// description and so could not be however they were written.

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

// A short fixed-capacity string. Structural, so it can be a template argument,
// where a `std::string_view` cannot.
struct Name {
  std::array<char, 15> storage{};
  // A byte, not a `std::size_t`: `Resolved` is a template argument, part of
  // every handler's identity, so a byte here is worth the seven it saves.
  std::uint8_t length{};
  constexpr Name() = default;
  template<std::size_t N>
  constexpr Name(const char (&text)[N]) : Name(std::string_view{text, N - 1}) {} // NOLINT(*-explicit-constructor)
  constexpr Name(const std::string_view text) { // NOLINT(*-explicit-constructor)
    if (text.size() > storage.size())
      throw std::length_error("name does not fit");
    std::ranges::copy(text, storage.begin());
    length = static_cast<std::uint8_t>(text.size());
  }
  static constexpr std::size_t capacity = std::tuple_size_v<decltype(storage)>;
  [[nodiscard]] constexpr std::string_view view() const { return {storage.data(), length}; }
  [[nodiscard]] constexpr bool empty() const { return length == 0; }
  constexpr bool operator==(const Name &) const = default;
};

// A string literal as a template argument, for the name of a description's
// file: what its diagnostics call it. Sized by the literal, since a file name
// has no reason to fit in a `Name`.
template<std::size_t N>
struct FileName {
  std::array<char, N> text{};
  constexpr FileName(const char (&literal)[N]) { // NOLINT(*-explicit-constructor)
    std::ranges::copy(literal, text.begin());
  }
  [[nodiscard]] constexpr std::string_view view() const { return {text.data(), N - 1}; }
};

// Marks a member function of a machine as one a description may name. A
// machine has an interface a description must not reach, so its operations
// are the members it marks, one by one, static or not:
//
//   [[=refract::operation]] Result verb(Operand operand);
//
// A palette the target lists needs none of this: it is a type built to be
// named, so every public static function in it is an operation.
struct Operation {};
inline constexpr Operation operation{};

// How a `.cpu` file spells an enumerator, when that differs from its C++ name,
// as a C++26 annotation (P3394) on the enumerator itself:
//
//   enum class Direction { Up [[=Spelling{"i"}]], Down [[=Spelling{"d"}]] };
//
// The spelling is a fact about the machine's assembly syntax, so it lives on
// the declaration. An annotation's type must be structural, which `Name` is
// and `std::string_view` is not.
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

// What a row wrote in an operand position: a constant, a name the machine can
// resolve, the immediate the encoding fetched, a vocabulary reference, or `-`
// to discard a result. A name is only a name here, whatever it is on the
// machine (the Z80's `a`, `hl` and `carry` are a register, a pair and a flag
// bit). Parentheses say to use it as an address, whichever kind it is.
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
  // The operand says which parameter it feeds rather than relying on where it
  // sits, as `value=(hl)` does in the Z80's description. Empty when the row
  // wrote it positionally, which is almost always. See `operand_for_parameter`
  // in Execute.hpp.
  Name parameter{};
  constexpr bool operator==(const Operand &) const = default;
};

// An operand once an opcode has settled which vocabulary member it meant.
// `Operand` is what a description wrote; this is what the generated code is
// built from, and `resolve` is the only way to arrive at one. It has no
// `Reference`, since that has been followed, and gains the five fields after
// `parameter`, which mean nothing until the member is known.
//
// A handler is a template on one of these, so every field is part of its
// identity: two operands that differ anywhere are two handlers.
struct Resolved {
  // No `Vocabulary`: resolving one is the lookup, so what is left names
  // whatever the member named.
  enum class Kind : std::uint8_t { Constant, Named, Immediate, Discard };
  Kind kind{};
  Name name{};
  std::uint16_t constant{};
  std::uint8_t width{};
  bool indirect{};
  bool displaced{};
  std::uint8_t write_back_delay{};
  Name parameter{};
  // The scope of the vocabulary this came from, because by the time a name is
  // looked up the vocabulary is long gone. Empty for an operand no vocabulary
  // owns, such as one a member appends, where the parameter decides.
  Name scope{};
  // The value is in the instruction: these bits of the opcode. A vocabulary of
  // plain numbers needs no code of its own, so choosing between its members is
  // a run-time read rather than one function per member.
  bool from_opcode{};
  BitSlice slice{};
  // Chosen by the table's view, so it cannot be folded away at compile time.
  // The generated code turns the vocabulary into the list of locations the view
  // indexes.
  bool from_view{};
  std::uint8_t view_vocabulary{};
  constexpr bool operator==(const Resolved &) const = default;
};

// An operand that names no vocabulary resolves to itself. A member's operand
// arrives here too, which is why the vocabulary case is a framework invariant
// rather than a diagnostic: the lexical parse cannot produce one, since it is
// the reference syntax that makes an operand a vocabulary reference and only a
// row can write it.
[[nodiscard]] constexpr Resolved as_resolved(const Operand &operand) {
  const auto kind = [&] {
    switch (operand.kind) {
      case Operand::Kind::Constant: return Resolved::Kind::Constant;
      case Operand::Kind::Named: return Resolved::Kind::Named;
      case Operand::Kind::Immediate: return Resolved::Kind::Immediate;
      case Operand::Kind::Discard: return Resolved::Kind::Discard;
      case Operand::Kind::Vocabulary: break;
    }
    throw std::logic_error("a vocabulary reference resolves to a member, never to itself");
  }();
  return {.kind = kind,
      .name = operand.name,
      .constant = operand.constant,
      .width = operand.width,
      .indirect = operand.indirect,
      .displaced = operand.displaced,
      .write_back_delay = operand.write_back_delay,
      .parameter = operand.parameter};
}

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

// One member of a vocabulary: the text a row's reference to it displays, the
// operation it may bind, and the operand it stands for in a step.
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
  // The text is an operand, parsed once here.
  Operand operand{};
  bool hole{};
  constexpr bool operator==(const Member &) const = default;
};

// A named list of members, one per value of the slice that selects among them.
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
// renamed; the Z80's `indexed` table is `base` read with
// `pair.hl -> {index:view}`. A rule names the vocabulary as well as the member,
// because the same text means different things in different vocabularies:
// `reg.h` is renamed by a view and the `real.h` of an indexed load is not. The
// right side is a whole member, so a substitute may bring its own access
// sequence.
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
// `bit = 0 1 2 3 4 5 6 7`. Its members differ in a value and nothing else, so
// the opcode can supply it at run time and no function per member is needed.
// Identity is required, not just numbers: a member that is a *function* of the
// slice, as in `rst = 0x00 0x08 ... 0x38`, would be read as its index.
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
// Passed by const reference: `Rules` holds whole members, so copying it per
// call would be paid for in constant evaluation.
struct Resolution {
  std::span<const Vocabulary> vocabularies{};
  Pattern matched{};
  std::span<const Rule> rules{};
  std::uint8_t opcode{};
  std::uint8_t view{};
};

// Whether two indirect operands address the same place: the same name or
// constant, reached the same way. Which is what makes a write-back a
// write-back, rather than a write through one address after a read through
// another.
[[nodiscard]] constexpr bool same_address(const Resolved &lhs, const Resolved &rhs) {
  return lhs.indirect && rhs.indirect && lhs.kind == rhs.kind && lhs.name == rhs.name && lhs.constant == rhs.constant &&
         lhs.displaced == rhs.displaced && lhs.from_opcode == rhs.from_opcode && lhs.slice == rhs.slice &&
         lhs.from_view == rhs.from_view && lhs.view_vocabulary == rhs.view_vocabulary;
}

// Which rule, if any, rewrites this member of this vocabulary. The two
// functions below must agree about which rule fires, since one returns the
// member it produces and the other where that member came from, so they ask the
// same question rather than each spelling it out.
[[nodiscard]] constexpr const Rule *rule_for(
    const std::span<const Rule> rules, const Reference reference, const std::string_view display) {
  const auto found = std::ranges::find_if(rules,
      [&](const Rule &rule) { return rule.vocabulary_index == reference.vocabulary_index && rule.from == display; });
  return found == rules.end() ? nullptr : &*found;
}

// Follows a reference to the member it names: the opcode's slice, or the
// table's view, says which, and the table's rules may rename it. This is the
// one place a reference is followed, so it is the one place renaming happens.
// A check asks with view 0 and trusts the answer for every view, which
// `check_view_vocabulary` in Parse.hpp makes sound.
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

// The one way from what a row wrote to what the generated code is built from. A
// reference names whichever vocabulary member its slice selects, and that
// member is written the same way an operand is written in a row, so most of
// this is deciding what the member could not know: which parameter it feeds,
// which scope its name belongs to, and whether the encoding or the view will
// answer at run time.
[[nodiscard]] constexpr Resolved resolve(const Resolution &at, const Operand &operand) {
  if (operand.kind != Operand::Kind::Vocabulary)
    return as_resolved(operand);
  auto result = as_resolved(member_of(at, operand.reference).operand);
  // The member supplies everything about the operand except which parameter it
  // was written against, which is the row's business and not the vocabulary's.
  result.parameter = operand.parameter;
  result.scope = Name{at.vocabularies[operand.reference.vocabulary_index].scope};
  // A number the opcode already carries: say where, rather than which. Every
  // member of the vocabulary then resolves to the same operand, so the
  // functions that differed only in a constant become one. `constant` is
  // cleared for sharing, not correctness: it is part of the handler's
  // identity, and the member's own value would split them again.
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
    result.view_vocabulary = vocabulary;
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
  // Z80 conditional is one.)
  enum class Kind : std::uint8_t { Apply, Goto, If };
  Kind kind{};
  // The table a `goto` hands decoding to.
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

// One line of a table: the encoding it matches, the mnemonic it renders, and
// the steps it runs, in order.
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

// A decoding table as declared: its name, the parent and renaming it derives
// from if it does, and the view it takes if it takes one.
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
// Spans, because the storage belongs to whoever did the parsing: a `Compiled`
// for a description a target names, a test for one of its own. Holding it as
// one value is what lets a consumer be handed "the table" rather than five of
// its parts.
struct Description {
  std::span<const Vocabulary> vocabularies;
  std::span<const Row> rows;
  std::span<const TableDecl> tables;
  std::span<const DecodeTable> decoded;
  // Decoding starts here. The format reserves no name for the entry table;
  // whoever built the description says which it is.
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
