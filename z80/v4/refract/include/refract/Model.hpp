#pragma once

// The shapes a parsed instruction table is made of, all of them plain value types. `Resolved` is a template argument
// later and `Spelling` an annotation, so both are *structural*: literal, with every member public, recursively, which
// is what `Name` exists to be. Nothing they do not hold has to be, so the rest are free to hold variants and
// `std::string_view`s into the description.

#include "refract/Continue.hpp"
#include "refract/Pattern.hpp"
#include "refract/Vector.hpp"
#include "refract/Visit.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace specbolt::refract {

// A short fixed-capacity string. Structural, so it can be a template argument, where a `std::string_view` cannot.
struct Name {
  std::array<char, 15> storage{};
  // A byte, not a `std::size_t`: `Resolved` is a template argument, part of every handler's identity, so a byte here is
  // worth the seven it saves.
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

// Marks a member function of a machine as one a description may name. A machine has an interface a description must not
// reach, so its operations are the members it marks, one by one, static or not:
//
//   [[=refract::operation]] Result verb(Operand operand);
//
// A palette the target lists needs none of this: it is a type built to be named, so every public static function in it
// is an operation.
struct Operation {};
inline constexpr Operation operation{};

// How a `.cpu` file spells an enumerator, when that differs from its C++ name, as a C++26 annotation (P3394) on the
// enumerator itself:
//
//   enum class Direction { Up [[=Spelling{"i"}]], Down [[=Spelling{"d"}]] };
//
// The spelling is a fact about the machine's assembly syntax, so it lives on the declaration. An annotation's type must
// be structural, which `Name` is and `std::string_view` is not.
struct Spelling {
  Name text{};
};

// Which vocabulary to look a value up in, and what says which of its members to take: a slice of the opcode, or (when
// `from_view` is set) the decoding table's own parameter, which a prefix chose and the instruction does not carry.
// Anything a row can write `{vocabulary:slice}` in holds one.
struct Reference {
  std::uint8_t vocabulary_index{};
  std::uint8_t slice_index{};
  bool from_view{};
  constexpr bool operator==(const Reference &) const = default;
};

// How an operand is reached, whatever kind it is: the same whether a row wrote it or a vocabulary member did, and
// carried unchanged from `Operand` to `Resolved`, which both derive from it. Parentheses say to use the operand as an
// address, whichever kind it is.
struct Access {
  bool indirect{};
  // The address is this operand offset by a displacement byte the instruction carries, as in the Z80's `(ix+d)`.
  // Forming it is the machine's job because paying for it is.
  bool displaced{};
  std::uint8_t write_back_delay{};
  // The operand says which parameter it feeds rather than relying on where it sits, as `value=(hl)` does in the Z80's
  // description. Empty when the row wrote it positionally, which is almost always. See `operand_for_parameter` in
  // Execute.hpp.
  Name parameter{};
  constexpr bool operator==(const Access &) const = default;
};

// What a row wrote in an operand position, and how it is reached. Each kind holds only what it needs.
struct Operand : Access {
  // A number written out.
  struct Constant {
    std::uint16_t value;
    constexpr bool operator==(const Constant &) const = default;
  };
  // A name the machine resolves. A name is only a name here, whatever it is on the machine (the Z80's `a`, `hl` and
  // `carry` are a register, a pair and a flag bit).
  struct Named {
    Name name;
    constexpr bool operator==(const Named &) const = default;
  };
  // The immediate the encoding fetched, of this many bytes.
  struct Immediate {
    std::uint8_t width;
    constexpr bool operator==(const Immediate &) const = default;
  };
  // Whichever member of a vocabulary the opcode's slice, or the view, selects.
  struct Vocabulary {
    Reference reference;
    constexpr bool operator==(const Vocabulary &) const = default;
  };
  // `-`: the result is dropped.
  struct Discard {
    constexpr bool operator==(const Discard &) const = default;
  };

  using Kind = std::variant<Constant, Named, Immediate, Vocabulary, Discard>;
  Kind kind;

  // One of each kind. A designated initialiser cannot name a base's member, so the aggregate-with-a-base spelling
  // lives here, once, rather than at every place the lexer makes one.
  [[nodiscard]] static constexpr Operand discard() { return {{}, Discard{}}; }
  [[nodiscard]] static constexpr Operand immediate(const std::uint8_t width) { return {{}, Immediate{width}}; }
  [[nodiscard]] static constexpr Operand literal(const std::uint16_t value) { return {{}, Constant{value}}; }
  [[nodiscard]] static constexpr Operand named(const Name name) { return {{}, Named{name}}; }
  [[nodiscard]] static constexpr Operand vocabulary(const Reference reference) { return {{}, Vocabulary{reference}}; }
};

// An operand once an opcode has settled which vocabulary member it meant. `Operand` is what a description wrote; this
// is what the generated code is built from. `resolve` makes one from what a row wrote, and `as_resolved` from an
// operand that names no vocabulary, such as an argument a member appends. It has no `Reference`, since that has been
// followed, and gains the fields below, which mean nothing until the member is known.
//
// A handler is a template on one of these, so every field is part of its identity: two operands that differ anywhere
// are two handlers.
struct Resolved : Access {
  // No `Vocabulary`: resolving one is the lookup, so what is left names whatever the member named. Flat rather than a
  // variant, which is not structural: the fields a kind does not use stay at their defaults.
  enum class Kind : std::uint8_t { Constant, Named, Immediate, Discard };
  Kind kind{};
  Name name{};
  std::uint16_t constant{};
  std::uint8_t width{};
  // The scope of the vocabulary this came from, because by the time a name is looked up the vocabulary is long gone.
  // Empty for an operand no vocabulary owns, such as one a member appends, where the parameter decides.
  Name scope{};
  // The value is in the instruction: these bits of the opcode. A vocabulary of plain numbers needs no code of its own,
  // so choosing between its members is a run-time read rather than one function per member.
  bool from_opcode{};
  BitSlice slice{};
  // Chosen by the table's view, so it cannot be folded away at compile time. The generated code turns the vocabulary
  // into the list of locations the view indexes.
  bool from_view{};
  std::uint8_t view_vocabulary{};
  constexpr bool operator==(const Resolved &) const = default;
};

// The `Resolved` form of an operand that names no vocabulary: the same access, with the kind carried across. A member's
// operand arrives here too, which is why the vocabulary case is a framework invariant rather than a diagnostic: the
// lexical parse cannot produce one, since it is the reference syntax that makes an operand a vocabulary reference and
// only a row can write it.
[[nodiscard]] constexpr Resolved as_resolved(const Operand &operand) {
  Resolved result{static_cast<const Access &>(operand)};
  refract::visit(Overloaded{
                     [&](const Operand::Constant &constant) {
                       result.kind = Resolved::Kind::Constant;
                       result.constant = constant.value;
                     },
                     [&](const Operand::Named &named) {
                       result.kind = Resolved::Kind::Named;
                       result.name = named.name;
                     },
                     [&](const Operand::Immediate &immediate) {
                       result.kind = Resolved::Kind::Immediate;
                       result.width = immediate.width;
                     },
                     [&](const Operand::Discard &) { result.kind = Resolved::Kind::Discard; },
                     [](const Operand::Vocabulary &) {
                       throw std::logic_error("a vocabulary reference resolves to a member, never to itself");
                     },
                 },
      operand.kind);
  return result;
}

// One part of an instruction's text, with the values it carries taken out: a literal chunk, a vocabulary member to look
// up, a value read from the encoding, or the displacement an indexed addressing mode carries. Each kind holds only what
// it needs, so a piece with no text cannot be asked for its text.
struct Piece {
  // Text rendered as written.
  struct Literal {
    std::string_view text;
    constexpr bool operator==(const Literal &) const = default;
  };
  // The member of a vocabulary that the encoding's slice, or the table's view, chooses; it renders its own pieces.
  struct Vocabulary {
    Reference reference;
    constexpr bool operator==(const Vocabulary &) const = default;
  };
  // The immediate the encoding fetched, one byte or two.
  struct Imm8 {
    constexpr bool operator==(const Imm8 &) const = default;
  };
  struct Imm16 {
    constexpr bool operator==(const Imm16 &) const = default;
  };
  // The displacement an indexed mode carries, rendered as one signed byte, which is the format's choice and not the
  // machine's.
  struct Displacement {
    constexpr bool operator==(const Displacement &) const = default;
  };
  // A relative jump's offset, rendered as the address it lands on.
  struct Relative {
    constexpr bool operator==(const Relative &) const = default;
  };

  std::variant<Literal, Vocabulary, Imm8, Imm16, Displacement, Relative> kind;
  constexpr bool operator==(const Piece &) const = default;
};

// One member of a vocabulary: the text a row's reference to it displays, and what it stands for, which is an operand,
// an operation for a step to apply, or nothing.
struct Member {
  // An operation, such as the Z80's `add:add8(0)`, and what this member decides about the call: the row fills the
  // arguments the encoding varies, and these are the rest.
  struct Operation {
    static constexpr std::size_t max_arguments = 3;
    std::string_view name;
    Vector<Operand, max_arguments> arguments{};
  };
  // `-`: no member here, so a row naming this one does not cover the opcode.
  struct Hole {};

  static constexpr std::size_t max_pieces = 3;
  std::string_view display{};
  // The display, split around whatever it renders from the instruction: an indexed mode writes its displacement inline,
  // so the disassembler renders rather than parses.
  Vector<Piece, max_pieces> pieces{};
  // An operand's text is the display itself, parsed once here.
  std::variant<Operand, Operation, Hole> kind{};
};

// A named list of members, one per value of the slice that selects among them.
struct Vocabulary {
  static constexpr std::size_t max_members = 8;
  std::string_view name{};
  // Which scope its members are looked up in, named by the `:` clause of a declaration such as the Z80's `vocab pair :
  // R16 = bc de hl sp`. Empty means the CPU's locations, which is what most of them are. Compared exactly, unlike a
  // member, because it names a C++ type rather than something written the way assembly is written.
  std::string_view scope{};
  Vector<Member, max_members> members{};
  // Whether member n is the number n, worked out once when the vocabulary is parsed (see `is_numeric`), because every
  // reference to it at every opcode asks.
  bool numeric{};
  // Where it was declared, so a check that fires elsewhere can point at the line that has to change. A continuation
  // folds to the line the declaration starts on.
  std::size_t line{};
};

// One renaming a derived table applies: the member of this vocabulary whose display is `from` reads as `to` instead. A
// derived table re-reads its parent's rows with some vocabulary members renamed; the Z80's `indexed` table is `base`
// read with `pair.hl -> {index:view}`. A rule names the vocabulary as well as the member, because the same text means
// different things in different vocabularies: `reg.h` is renamed by a view and the `real.h` of an indexed load is not.
// The right side is a whole member, so a substitute may bring its own addressing mode and write-back delay.
struct Rule {
  std::uint8_t vocabulary_index{};
  std::string_view from{};
  Member to{};
  // The replacement is chosen by the table's view rather than fixed, which is what lets one table stand for every
  // member the view can select: the Z80's `pair.hl -> {index:view}` covers `ix` and `iy` at once.
  bool to_is_view{};
  std::uint8_t to_vocabulary{};
};

// The renamings one table applies to what it decodes.
using Rules = Vector<Rule, 6>;

// Whether a vocabulary *is* its slice: member n is the plain number n, as in the Z80's `bit = 0 1 2 3 4 5 6 7`. Its
// members differ in a value and nothing else, so the opcode can supply it at run time and no function per member is
// needed. Identity is required, not just numbers: a member that is a *function* of the slice, as the Z80's
// `rst = 0x00 0x08 ... 0x38` is, would be read as its index, and a member written as an address, such as `(1)`, names
// the memory there rather than the number.
[[nodiscard]] constexpr bool is_numeric(const Vocabulary &vocabulary) {
  auto any = false;
  for (const auto [at, member]: std::views::enumerate(vocabulary.members)) {
    // Whether this member is the number `at`; a hole says nothing either way.
    const auto fits = refract::visit(
        Overloaded{
            [&](const Operand &operand) {
              any = true;
              // A displacement is only ever written inside an address, so this rules out both.
              if (operand.indirect)
                return false;
              return refract::visit(Overloaded{
                                        [&](const Operand::Constant &constant) { return constant.value == at; },
                                        [](const OneOf<Operand::Named, Operand::Immediate, Operand::Vocabulary,
                                            Operand::Discard> auto &) { return false; },
                                    },
                  operand.kind);
            },
            [](const Member::Operation &) { return false; },
            [](const Member::Hole &) { return true; },
        },
        member.kind);
    if (!fits)
      return false;
  }
  return any;
}

// Where a reference is resolved: the vocabularies to look in, the encoding the row matched, the opcode that selects
// within it, the renaming the table applies to what it decodes, and the view a prefix chose.
struct Resolution {
  std::span<const Vocabulary> vocabularies{};
  Pattern matched{};
  std::span<const Rule> rules{};
  std::uint8_t opcode{};
  std::uint8_t view{};
};

// Whether two indirect operands address the same place: the same name or constant, reached the same way. Which is what
// makes a write-back a write-back, rather than a write through one address after a read through another, and what
// `displaced_through` in Decode.hpp means by an instruction being displaced through one base.
[[nodiscard]] constexpr bool same_address(const Resolved &lhs, const Resolved &rhs) {
  return lhs.indirect && rhs.indirect && lhs.kind == rhs.kind && lhs.name == rhs.name && lhs.constant == rhs.constant &&
         lhs.displaced == rhs.displaced && lhs.from_opcode == rhs.from_opcode && lhs.slice == rhs.slice &&
         lhs.from_view == rhs.from_view && lhs.view_vocabulary == rhs.view_vocabulary;
}

// The rule that rewrites this member of this vocabulary, or null when none does. The two functions below must agree
// about which rule fires, since one returns the member it produces and the other where that member came from, so they
// ask the same question rather than each spelling it out.
[[nodiscard]] constexpr const Rule *rule_for(
    const std::span<const Rule> rules, const Reference reference, const std::string_view display) {
  const auto found = std::ranges::find_if(rules,
      [&](const Rule &rule) { return rule.vocabulary_index == reference.vocabulary_index && rule.from == display; });
  return found == rules.end() ? nullptr : &*found;
}

// Follows a reference to the member it names: the opcode's slice, or the table's view, says which, and the table's
// rules may rename it. `source_of` follows a reference the same way to say where the member came from; the two share
// `rule_for` so that they agree about which rule fires. A check asks with view 0 and trusts the answer for every view,
// which `check_view_vocabulary` in Parse.hpp makes sound for the member's shape, on the assumption about rules that
// `source_of` states.
[[nodiscard]] constexpr Member member_of(const Resolution &at, const Reference reference) {
  const auto which = reference.from_view ? at.view : at.matched.slices[reference.slice_index].extract(at.opcode);
  const auto &member = at.vocabularies[reference.vocabulary_index].members[which];
  if (const auto *rule = rule_for(at.rules, reference, member.display))
    return rule->to_is_view ? at.vocabularies[rule->to_vocabulary].members[at.view] : rule->to;
  return member;
}

// Which vocabulary a reference finally lands in, as an index into `at.vocabularies`, and whether the view chose the
// member. Only `resolve` needs this: an operand the view chose must name the vocabulary rather than the member, because
// the member is not known yet.
[[nodiscard]] constexpr std::pair<std::uint8_t, bool> source_of(const Resolution &at, const Reference reference) {
  // Member 0 stands for every member the view could select. That assumes no rule names a member of a vocabulary a view
  // selects: rules match by display text, which differs between those members and which `check_view_vocabulary` does
  // not compare, so such a rule would fire for one view and not the others. Nothing checks the assumption.
  const std::size_t which = reference.from_view ? 0u : at.matched.slices[reference.slice_index].extract(at.opcode);
  const auto &member = at.vocabularies[reference.vocabulary_index].members[which];
  if (const auto *rule = rule_for(at.rules, reference, member.display))
    return {rule->to_vocabulary, rule->to_is_view};
  return {reference.vocabulary_index, reference.from_view};
}

// The member a row's vocabulary reference selects, resolved as the operand the row wrote there: `resolve` for the
// one kind of operand that needs a lookup.
[[nodiscard]] constexpr Resolved resolve_reference(
    const Resolution &at, const Operand &operand, const Reference reference) {
  const auto member = member_of(at, reference);
  auto result =
      refract::visit(Overloaded{
                         [](const Operand &stands_for) { return as_resolved(stands_for); },
                         [&](const Member::Operation &) -> Resolved {
                           throw std::runtime_error("'" + std::string(member.display) +
                                                    "' is an operation, and this row names it where an operand "
                                                    "belongs");
                         },
                         [](const Member::Hole &) -> Resolved {
                           throw std::logic_error("a hole never decodes, so it is never resolved");
                         },
                     },
          member.kind);
  // The member supplies everything about the operand except which parameter it was written against, which is the row's
  // business and not the vocabulary's.
  result.parameter = operand.parameter;
  result.scope = Name{at.vocabularies[reference.vocabulary_index].scope};
  // A number the opcode already carries: say where, rather than which. Every member of the vocabulary then resolves to
  // the same operand, so the functions that differed only in a constant become one. `constant` is cleared for sharing,
  // not correctness: it is part of the handler's identity, and the member's own value would split them again.
  if (!reference.from_view && at.vocabularies[reference.vocabulary_index].numeric) {
    result.from_opcode = true;
    result.slice = at.matched.slices[reference.slice_index];
    result.constant = 0;
    return result;
  }
  // The member supplies the shape (indirect, displaced, what a write-back idles for) but *which* member is not known
  // until the table's view has been chosen, so the vocabulary is carried instead of a name. The generated code turns it
  // into the list of locations the view selects between.
  if (const auto [vocabulary, from_view] = source_of(at, reference); from_view) {
    result.from_view = true;
    result.view_vocabulary = vocabulary;
  }
  return result;
}

// Resolves an operand against this opcode: a vocabulary reference becomes the member its slice, or the view, selects,
// and anything else resolves to itself.
[[nodiscard]] constexpr Resolved resolve(const Resolution &at, const Operand &operand) {
  return refract::visit(
      Overloaded{
          [&](const Operand::Vocabulary &written) { return resolve_reference(at, operand, written.reference); },
          [&](const OneOf<Operand::Constant, Operand::Named, Operand::Immediate, Operand::Discard> auto &) {
            return as_resolved(operand);
          },
      },
      operand.kind);
}

// How many operands, and how many destinations, one step may name.
inline constexpr std::size_t max_operands = 4;

// A row that hands decoding to another table, written `goto name`: a prefix. It is the whole of its row, and renders
// and does nothing itself, because what follows it is the instruction.
struct Transfer {
  // The table decoding continues in.
  std::uint8_t target{};
  // Which member of the target's view vocabulary it is decoded under, when the goto names one, as the Z80's
  // `goto indexed(ix)` does.
  std::uint8_t target_view{};
  // Or the view this table was decoded under, handed on, when the goto names this table's own view parameter (as the
  // Z80's `goto indexed_cb(view)` does).
  bool forwards_view{};
};

// One step of a row: an operation applied to its operands, with its result written to its destinations. A step whose
// operation returns `Continue` is a condition, and decides whether the steps after it run. That is the whole of what a
// condition can do here: there is no way to guard a step in the middle and resume after it, so a description whose
// conditionals are not a tail cannot be written. (Every Z80 conditional is one.) A row's steps run in order, and that
// order is where cost lives: an internal delay is a step like any other.
struct Step {
  std::string_view operation{};
  std::optional<Reference> operation_reference{};
  Vector<Operand, max_operands> destinations{};
  Vector<Operand, max_operands> operands{};
};

// One line of a table: the encoding it matches, the mnemonic it renders, and what it does.
struct Row {
  static constexpr std::size_t max_pieces = 12;
  static constexpr std::size_t max_steps = 6;
  Pattern matched{};
  std::string_view mnemonic{};
  Vector<Piece, max_pieces> pieces{};
  std::uint8_t immediate_bytes{};
  // `d` in the encoding: this row reads a displacement it does not use itself, and hands it to the table it goes to.
  // What it is for is an encoding whose opcode byte is not its last, as in the Z80's `dd cb d op`.
  bool reads_displacement{};
  std::uint8_t table{};
  // What the row does: its steps, in order, or a transfer to another table.
  using Steps = Vector<Step, max_steps>;
  std::variant<Steps, Transfer> action{};
  std::size_t line{};
};

// A decoding table as declared: its name, the parent and renaming it derives from if it does, and the view it takes if
// it takes one.
struct TableDecl {
  std::string_view name{};
  std::size_t line{};
  // A derived table decodes its parent's rows under `rules`, and may carry rows of its own that override them.
  bool derived{};
  std::uint8_t parent{};
  Rules rules{};
  // A table declared `t(view:v)` is decoded once for each member of `v` without being generated once for each; the
  // Z80's is `table indexed(view:index)`. The name is what a row writes where a slice letter would go; empty means the
  // table takes no view.
  std::string_view view_name{};
  std::uint8_t view_vocabulary{};
  [[nodiscard]] constexpr bool takes_view() const { return !view_name.empty(); }
};

// Where a row hands decoding on to, or nothing for a row that is an instruction itself.
[[nodiscard]] constexpr std::optional<Transfer> transfer_of(const Row &row) {
  return refract::visit(Overloaded{
                            [](const Row::Steps &) -> std::optional<Transfer> { return std::nullopt; },
                            [](const Transfer &transfer) -> std::optional<Transfer> { return transfer; },
                        },
      row.action);
}

// The steps a row runs, in order: none for a transfer, which does nothing but hand decoding on.
[[nodiscard]] constexpr std::span<const Step> steps_of(const Row &row) {
  return refract::visit(Overloaded{
                            [](const Row::Steps &steps) { return std::span<const Step>{steps}; },
                            [](const Transfer &) { return std::span<const Step>{}; },
                        },
      row.action);
}

// Which row, as an index into the description's rows, a table decodes each of its 256 opcodes to, or nothing where no
// row claims one.
using DecodeTable = std::array<std::optional<std::size_t>, 256>;

// A whole parsed description, as everything downstream of the parse sees it. Spans, because the storage belongs to
// whoever did the parsing: a `Compiled` for a description a target names, a test for one of its own. Holding it as one
// value is what lets a consumer be handed "the table" rather than five of its parts.
struct Description {
  std::span<const Vocabulary> vocabularies;
  std::span<const Row> rows;
  std::span<const TableDecl> tables;
  std::span<const DecodeTable> decoded;
  // Decoding starts here. The format reserves no name for the entry table; whoever built the description says which it
  // is.
  std::uint8_t entry{};

  // What this table decodes this opcode to, or nothing. Every table is total in a description that passes its checks,
  // but this is what those checks are written against, so it does not assume it.
  [[nodiscard]] constexpr const Row *row_for(const std::uint8_t table, const std::uint8_t opcode) const {
    const auto index = decoded[table][opcode];
    return index ? &rows[*index] : nullptr;
  }

  // The renaming every row this table decodes is read under: its own rows as well as the ones it inherits.
  [[nodiscard]] constexpr const Rules &rules_for(const std::uint8_t table) const { return tables[table].rules; }
};

} // namespace specbolt::refract
