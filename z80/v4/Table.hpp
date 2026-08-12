#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/v4/Matched.hpp"
#include "z80/v4/Parser.hpp"
#include "z80/v4/TableError.hpp"
#include "z80/v4/Vector.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#endif

namespace specbolt::v4 {

// clang-format off
inline constexpr char cpu_raw[] = {
#embed SPECBOLT_CPU_TABLE
  , 0
};
// clang-format on

inline constexpr std::string_view cpu_description{cpu_raw};

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

// A table that renames nothing, which is every table but a derived one.
inline constexpr Rules no_rules{};

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

[[nodiscard]] constexpr std::string_view trim_comma(std::string_view text) {
  if (text.ends_with(','))
    text.remove_suffix(1);
  return text;
}

// A keyword on its own is still that keyword, so `table` with no name reaches
// the diagnostic that says so rather than being silently ignored.
[[nodiscard]] constexpr bool is_directive(const std::string_view line, const std::string_view keyword) {
  return line.starts_with(keyword) && (line.size() == keyword.size() || line[keyword.size()] == ' ');
}
[[nodiscard]] constexpr bool is_field(const std::string_view line) { return is_directive(line, "field"); }
[[nodiscard]] constexpr bool is_table(const std::string_view line) { return is_directive(line, "table"); }
[[nodiscard]] constexpr bool is_row(const std::string_view line) {
  return !line.empty() && line.front() != '#' && !is_field(line) && !is_table(line) && line.contains('|');
}

[[nodiscard]] constexpr std::size_t count_matching(
    const std::string_view description, bool (*predicate)(std::string_view)) {
  Parser lines(description);
  std::size_t count = 0;
  while (!lines.eof())
    if (predicate(lines.next_line().text))
      ++count;
  return count;
}

[[nodiscard]] constexpr std::uint8_t parse_delay(const std::string_view value, const std::size_t line) {
  if (value.size() != 1 || value.front() < '0' || value.front() > '9')
    throw table_error(line, "delay must be a single digit");
  return static_cast<std::uint8_t>(value.front() - '0');
}

[[nodiscard]] constexpr Operand parse_simple_operand(
    std::string_view word, const std::size_t line, const std::uint8_t immediate_bytes) {
  if (word.empty())
    throw table_error(line, "empty operand in action");
  // An addressing mode written out in a row says what it costs the same way a
  // vocabulary member does.
  if (const auto slash = word.find('/'); slash != std::string_view::npos) {
    Parser attribute(word.substr(slash + 1));
    if (attribute.split_to('=').data() != "delay")
      throw table_error(line, "'" + std::string(word.substr(slash + 1)) + "' is not an operand attribute");
    auto attributed = parse_simple_operand(word.substr(0, slash), line, immediate_bytes);
    attributed.write_back_delay = parse_delay(attribute.data(), line);
    return attributed;
  }
  if (word == "-")
    return {.kind = Operand::Kind::Discard};
  if (word.starts_with('(')) {
    if (!word.ends_with(')'))
      throw table_error(line, "unterminated '(' in operand '" + std::string(word) + "'");
    word = word.substr(1, word.size() - 2);
    const auto displaced = word.ends_with("+d");
    if (displaced)
      word.remove_suffix(2);
    auto addressed = parse_simple_operand(word, line, immediate_bytes);
    if (addressed.indirect)
      throw table_error(line, "an address cannot itself be indirect");
    addressed.indirect = true;
    addressed.displaced = displaced;
    return addressed;
  }
  if (word.ends_with("+d"))
    throw table_error(line, "a displacement only makes sense inside '(...)'");
  if (word == "n")
    return {.kind = Operand::Kind::Immediate, .width = immediate_bytes};
  if (word == "nn")
    throw table_error(line, "write 'n'; the encoding column says how many bytes it occupies");
  if (word.front() >= '0' && word.front() <= '9') {
    const auto hex = word.starts_with("0x");
    const auto digits = hex ? word.substr(2) : word;
    const auto base = hex ? 16u : 10u;
    if (digits.empty())
      throw table_error(line, "malformed constant '" + std::string(word) + "'");
    unsigned value = 0;
    for (const auto character: digits) {
      const auto digit = character >= '0' && character <= '9'   ? static_cast<unsigned>(character - '0')
                         : character >= 'a' && character <= 'f' ? static_cast<unsigned>(character - 'a' + 10)
                                                                : base;
      if (digit >= base)
        throw table_error(line, "malformed constant '" + std::string(word) + "'");
      value = value * base + digit;
      if (value > 0xffff)
        throw table_error(line, "constant '" + std::string(word) + "' does not fit in 16 bits");
    }
    return {.kind = Operand::Kind::Constant, .constant = static_cast<std::uint16_t>(value)};
  }
  if (word.size() > Name::capacity)
    throw table_error(line, "operand name '" + std::string(word) + "' is too long");
  return {.kind = Operand::Kind::Named, .name = Name{word}};
}

// Splits display text around the values it renders rather than spells: `$nn`
// and `$nnnn` come from the encoding, `+d` is the displacement an indexed mode
// carries. Both a row's mnemonic and a vocabulary member's text are lowered
// with this, so neither is parsed at runtime.
constexpr void lower_text(Parser text, const auto &push, const std::size_t line) {
  const auto push_immediates = [&](Parser chunk) {
    while (!chunk.eof()) {
      if (!chunk.data().contains('$')) {
        if (!chunk.data().empty())
          push(Piece{.kind = Piece::Kind::Literal, .text = chunk.data()});
        return;
      }
      if (const auto literal = chunk.split_to('$').data(); !literal.empty())
        push(Piece{.kind = Piece::Kind::Literal, .text = literal});
      if (chunk.data().starts_with('e')) {
        chunk.skip_any("e");
        push(Piece{.kind = Piece::Kind::Relative});
        continue;
      }
      const auto remaining = chunk.data().size();
      chunk.skip_any("n");
      switch (remaining - chunk.data().size()) {
        case 2: push(Piece{.kind = Piece::Kind::Imm8}); break;
        case 4: push(Piece{.kind = Piece::Kind::Imm16}); break;
        default: throw table_error(line, "expected $nn, $nnnn or $e in mnemonic");
      }
    }
  };

  while (!text.eof()) {
    const auto at = text.data().find("+d");
    if (at == std::string_view::npos) {
      push_immediates(text);
      return;
    }
    push_immediates(Parser(text.data().substr(0, at)));
    push(Piece{.kind = Piece::Kind::Displacement});
    text = Parser(text.data().substr(at + 2));
  }
}

// `bc` is display only; `adc:add8+carry` binds a primitive and appends an
// operand; `(hl)/delay=1` states the access sequence of an addressing mode.
[[nodiscard]] constexpr Member parse_member(const std::string_view text, const std::size_t line) {
  Parser whole(text);
  Parser parser(whole.split_to('/').data());
  Member member{.display = parser.split_to(':').data(), .primitive = parser.data()};
  if (const auto attributes = whole.data(); !attributes.empty()) {
    Parser attribute(attributes);
    const auto key = attribute.split_to('=').data();
    const auto value = attribute.data();
    if (key != "delay")
      throw table_error(line, "'" + std::string(key) + "' is not a member attribute; expected 'delay'");
    member.write_back_delay = parse_delay(value, line);
  }
  if (member.display.empty())
    throw table_error(line, "field member has no name");
  if (member.display == "-") {
    member.hole = true;
    return member;
  }
  lower_text(
      Parser(member.display),
      [&](const Piece piece) { member.pieces.push_back(piece, line, "member text is too complicated"); }, line);
  member.operand = parse_simple_operand(member.display, line, 0);
  if (member.operand.kind == Operand::Kind::Immediate || member.operand.kind == Operand::Kind::Discard)
    throw table_error(line, "a vocabulary member must name something the CPU can resolve");
  Parser primitive(member.primitive);
  member.primitive = primitive.split_to('+').data();
  if (const auto appended = primitive.data(); !appended.empty()) {
    member.appended = parse_simple_operand(appended, line, 0);
    if (member.appended->kind == Operand::Kind::Immediate)
      throw table_error(line, "a vocabulary member cannot append an immediate; only the encoding fetches those");
  }
  return member;
}

template<std::size_t N>
[[nodiscard]] constexpr std::array<Field, N> parse_fields(const std::string_view description) {
  std::array<Field, N> result{};
  Parser lines(description);
  std::size_t index = 0;
  while (!lines.eof()) {
    const auto [at, text] = lines.next_line();
    if (!is_field(text))
      continue;
    Parser parser(text);
    static_cast<void>(parser.next_word());
    auto &field = result[index++];
    const auto name = parser.next_word();
    if (name.size() != 1)
      throw table_error(at, "field name must be a single character");
    field.name = name.front();
    if (parser.next_word() != "=")
      throw table_error(at, "expected '=' in field declaration");
    while (!parser.eof()) {
      const auto value = parser.next_word();
      if (value.empty())
        continue;
      field.values.push_back(parse_member(value, at), at, "too many values in field");
    }
    if (field.values.empty())
      throw table_error(at, "field declares no values");
    for (std::size_t other = 0; other + 1 < index; ++other)
      if (result[other].name == field.name)
        throw table_error(at, "duplicate field name");
  }
  return result;
}

struct TableDecl {
  std::string_view name{};
  std::size_t line{};
  // A derived table decodes its parent's rows under `rules`, and may carry rows
  // of its own that override them.
  bool derived{};
  std::uint8_t parent{};
  Rules rules{};
};

[[nodiscard]] constexpr std::optional<std::size_t> find_field(const std::span<const Field> fields, const char name) {
  for (std::size_t index = 0; index < fields.size(); ++index)
    if (fields[index].name == name)
      return index;
  return std::nullopt;
}

// `p.hl->ix, r.h -> ixh`: either spacing, because both read naturally.
constexpr void parse_substitutions(
    const std::string_view text, const std::span<const Field> fields, TableDecl &table, const std::size_t line) {
  Parser list(text);
  while (!list.eof()) {
    const auto rule = Parser::trim(list.split_to(',').data());
    if (rule.empty())
      continue;
    const auto arrow = rule.find("->");
    if (arrow == std::string_view::npos)
      throw table_error(line, "expected '->' in table substitution '" + std::string(rule) + "'");
    const auto left = Parser::trim(rule.substr(0, arrow));
    const auto to = Parser::trim(rule.substr(arrow + 2));
    if (left.empty() || to.empty())
      throw table_error(line, "a table substitution needs a name on each side of '->'");
    const auto dot = left.find('.');
    if (dot == std::string_view::npos)
      throw table_error(line, "a table substitution names the vocabulary it rewrites, as in 'r.h -> ixh'");
    const auto vocabulary = left.substr(0, dot);
    if (vocabulary.size() != 1)
      throw table_error(line, "vocabulary name must be a single character");
    const auto field = find_field(fields, vocabulary.front());
    if (!field)
      throw table_error(line, "substitution names a vocabulary that does not exist");
    const auto from = left.substr(dot + 1);
    if (from.empty())
      throw table_error(line, "a table substitution needs a name on each side of '->'");
    if (std::ranges::none_of(fields[*field].values, [&](const Member &m) { return m.display == from; }))
      throw table_error(line, "vocabulary '" + std::string(vocabulary) + "' has no member '" + std::string(from) + "'");
    table.rules.push_back(
        {static_cast<std::uint8_t>(*field), from, parse_member(to, line)}, line, "too many substitutions in table");
  }
}

template<std::size_t N>
[[nodiscard]] constexpr std::array<TableDecl, N> parse_tables(
    const std::string_view description, const std::span<const Field> fields) {
  std::array<TableDecl, N> result{};
  Parser lines(description);
  std::size_t index = 0;
  while (!lines.eof()) {
    const auto [at, text] = lines.next_line();
    if (!is_table(text))
      continue;
    Parser parser(text);
    static_cast<void>(parser.next_word());
    const auto name = parser.next_word();
    if (name.empty())
      throw table_error(at, "table declaration has no name");
    for (std::size_t other = 0; other < index; ++other)
      if (result[other].name == name)
        throw table_error(at, "duplicate table name");
    auto &table = result[index++];
    table = {.name = name, .line = at};
    if (const auto equals = parser.next_word(); equals.empty())
      continue;
    else if (equals != "=")
      throw table_error(at, "expected '= <parent> with <substitutions>' after the table name");
    // Only a table already declared, which makes the derivation a forest: a
    // parent's own rows are resolved before anything inherits them.
    const auto parent = parser.next_word();
    table.derived = true;
    table.parent = 0xff;
    for (std::size_t other = 0; other + 1 < index; ++other)
      if (result[other].name == parent)
        table.parent = static_cast<std::uint8_t>(other);
    if (table.parent == 0xff)
      throw table_error(at, "no table named '" + std::string(parent) + "' is declared above this one");
    if (parser.next_word() != "with")
      throw table_error(at, "expected 'with' after the parent table name");
    parse_substitutions(parser.data(), fields, table, at);
    if (table.rules.empty())
      throw table_error(at, "a derived table declares no substitutions, so it is its parent");
  }
  return result;
}

[[nodiscard]] constexpr std::uint8_t find_table(
    const std::span<const TableDecl> tables, const std::string_view name, const std::size_t line) {
  for (std::size_t index = 0; index < tables.size(); ++index)
    if (tables[index].name == name)
      return static_cast<std::uint8_t>(index);
  throw table_error(line, "no table named '" + std::string(name) + "'");
}

[[nodiscard]] constexpr std::optional<std::size_t> find_slice(const Matched &matched, const char name) {
  const auto found = std::ranges::find(matched.slices, name, &BitSlice::name);
  if (found == matched.slices.end())
    return std::nullopt;
  return static_cast<std::size_t>(found - matched.slices.begin());
}

// `{p}` names one letter for both; `{r:z}` binds vocabulary r to slice z.
[[nodiscard]] constexpr Reference parse_reference(
    const std::span<const Field> fields, const std::string_view inner, const Matched &matched, const std::size_t line) {
  Parser parser(inner);
  const auto vocabulary = parser.split_to(':').data();
  const auto slice = parser.eof() ? vocabulary : parser.data();
  if (vocabulary.size() != 1 || slice.size() != 1)
    throw table_error(line, "reference must be {x} or {vocabulary:slice}");
  const auto field = find_field(fields, vocabulary.front());
  if (!field)
    throw table_error(line, "reference names a vocabulary that does not exist");
  const auto found = find_slice(matched, slice.front());
  if (!found)
    throw table_error(line, "reference names a field the opcode pattern does not define");
  if (fields[*field].values.size() != std::size_t{matched.slices[*found].mask} + 1)
    throw table_error(line, "vocabulary has the wrong number of values for its opcode bits");
  return {static_cast<std::uint8_t>(*field), static_cast<std::uint8_t>(*found)};
}

[[nodiscard]] constexpr Reference reference_from_braces(
    const std::span<const Field> fields, const std::string_view text, const Matched &matched, const std::size_t line) {
  if (!text.starts_with('{') || !text.ends_with('}'))
    throw table_error(line, "reference must be {x} or {vocabulary:slice}");
  return parse_reference(fields, text.substr(1, text.size() - 2), matched, line);
}

[[nodiscard]] constexpr Operand parse_operand(const std::span<const Field> fields, const std::string_view word,
    const Matched &matched, const std::size_t line, const std::uint8_t immediate_bytes) {
  if (!word.starts_with('{'))
    return parse_simple_operand(word, line, immediate_bytes);
  return {.kind = Operand::Kind::Field, .reference = reference_from_braces(fields, word, matched, line)};
}

constexpr void lower_mnemonic(const std::span<const Field> fields, Row &row) {
  const auto push = [&row](const Piece piece) { row.pieces.push_back(piece, row.line, "mnemonic is too complicated"); };
  const auto push_text = [&](const Parser text) { lower_text(text, push, row.line); };

  Parser parser(row.mnemonic);
  while (!parser.eof()) {
    if (!parser.data().contains('{')) {
      push_text(parser);
      return;
    }
    push_text(Parser(parser.split_to('{').data()));
    if (!parser.data().contains('}'))
      throw table_error(row.line, "unterminated field reference in mnemonic");
    push({.kind = Piece::Kind::Field,
        .reference = parse_reference(fields, parser.split_to('}').data(), row.matched, row.line)});
  }
}

// The encoding says what is fetched. The mnemonic must render exactly that, and
// the action must use it: otherwise one of the three columns is lying.
constexpr void check_immediates(const Row &row) {
  std::size_t rendered = 0;
  for (const auto &piece: row.pieces)
    rendered += piece.kind == Piece::Kind::Imm8 || piece.kind == Piece::Kind::Relative ? 1u
                : piece.kind == Piece::Kind::Imm16                                     ? 2u
                                                                                       : 0u;
  if (rendered != row.immediate_bytes)
    throw table_error(row.line, "the mnemonic renders a different number of immediate bytes than the encoding fetches");

  const auto immediate = [](const Operand &operand) { return operand.kind == Operand::Kind::Immediate; };
  const auto uses_immediate = std::ranges::any_of(row.steps, [&](const Step &step) {
    return std::ranges::any_of(step.operands, immediate) || std::ranges::any_of(step.destinations, immediate);
  });
  if (uses_immediate != (row.immediate_bytes != 0))
    throw table_error(row.line, "the action and the encoding disagree about whether there is an immediate");
}

template<std::size_t N>
[[nodiscard]] constexpr std::array<Row, N> parse_rows(
    const std::string_view description, const std::span<const Field> fields, const std::span<const TableDecl> tables) {
  std::array<Row, N> result{};
  Parser lines(description);
  std::size_t index = 0;
  std::optional<std::uint8_t> current;
  while (!lines.eof()) {
    const auto [at, text] = lines.next_line();
    if (is_table(text)) {
      Parser declaration(text);
      static_cast<void>(declaration.next_word());
      current = find_table(tables, declaration.next_word(), at);
      continue;
    }
    if (!is_row(text))
      continue;
    if (!current)
      throw table_error(at, "this row is not in any table; declare one with `table <name>` first");
    Parser parser(text, at);
    auto &row = result[index++];
    row.line = at;
    row.table = *current;
    Parser encoding(Parser::trim(parser.split_to('|').data()), at);
    row.matched = parse_opcode_bits(encoding.next_word(), at);
    while (!encoding.eof()) {
      const auto token = encoding.next_word();
      if (token.empty())
        continue;
      if (token == "d") {
        if (row.reads_displacement)
          throw table_error(at, "a row reads at most one displacement");
        row.reads_displacement = true;
        continue;
      }
      if (token != "n")
        throw table_error(at, "'" + std::string(token) + "' is not an encoding byte; expected 'n' or 'd'");
      ++row.immediate_bytes;
    }
    if (row.immediate_bytes > 2)
      throw table_error(at, "an instruction may carry at most two immediate bytes");
    row.mnemonic = Parser::trim(parser.split_to('|').data());
    // steps run in order, separated by `;`
    Parser sequence(Parser::trim(parser.data()));
    while (!sequence.eof()) {
      Parser action(Parser::trim(sequence.split_to(';').data()));
      if (action.eof())
        continue;
      row.steps.push_back({}, at, "row has too many steps");
      auto &step = row.steps[row.steps.size() - 1];
      step.verb = action.next_word();
      if (step.verb == "if") {
        step.kind = Step::Kind::If;
        step.verb = action.next_word();
        if (step.verb.empty())
          throw table_error(at, "'if' needs something to test");
      }
      if (step.verb == "goto") {
        if (step.kind == Step::Kind::If)
          throw table_error(at, "a goto cannot be conditional; guard it with an earlier `if` step");
        step.kind = Step::Kind::Goto;
        step.target = find_table(tables, action.next_word(), at);
        if (!action.next_word().empty())
          throw table_error(at, "goto takes a single table name");
        continue;
      }
      if (step.verb.starts_with('{'))
        step.verb_reference = reference_from_braces(fields, step.verb, row.matched, at);
      // `verb dest <- args...`; the destination is optional
      auto writing_destination = action.data().contains("<-");
      while (!action.eof()) {
        const auto word = trim_comma(action.next_word());
        if (word.empty())
          continue;
        if (word == "<-") {
          writing_destination = false;
          continue;
        }
        const auto operand = parse_operand(fields, word, row.matched, at, row.immediate_bytes);
        if (writing_destination) {
          step.destinations.push_back(operand, at, "too many destinations");
        }
        else {
          if (operand.kind == Operand::Kind::Discard)
            throw table_error(at, "'-' discards a result, so it can only be a destination");
          step.operands.push_back(operand, at, "too many operands");
        }
      }
    }
    if (row.steps.empty())
      throw table_error(at, "row has no action");
    // The disassembler renders nothing for a goto row and stops, so a goto has
    // to be the whole row or the two would disagree about what an opcode means.
    if (std::ranges::any_of(row.steps, [](const Step &step) { return step.kind == Step::Kind::Goto; }) &&
        row.steps.size() != 1) // NOLINT
      throw table_error(at, "a goto must be the row's only step");
    lower_mnemonic(fields, row);
    check_immediates(row);
  }
  return result;
}

// A field operand names whichever vocabulary member its slice selects, and that
// member is written the same way an operand is written in a row.
[[nodiscard]] constexpr Operand resolve(const std::span<const Field> fields, const Operand operand,
    const Matched &matched, const std::uint8_t opcode, const Rules &rules = {}) {
  if (operand.kind != Operand::Kind::Field)
    return operand;
  const auto member = member_of(fields, operand.reference, matched, opcode, rules);
  auto result = member.operand;
  result.write_back_delay = member.write_back_delay;
  return result;
}

// What this opcode, decoded here, is displaced through -- nothing if it is not.
// Nothing declares this: a row says `{r:z}`, a view says that member is now
// `(ix+d)`, and the answer is whatever the operands resolve to.
//
// One per instruction, not one per operand. `inc (ix+d)` reads and writes
// through the same address, and the chip reads one displacement and forms one
// sum; forming it per operand would pay for it twice.
[[nodiscard]] constexpr std::optional<Operand> displaced_through(
    const std::span<const Field> fields, const Row &row, const std::uint8_t opcode, const Rules &rules) {
  std::optional<Operand> found;
  const auto consider = [&](const Operand &operand) {
    const auto resolved = resolve(fields, operand, row.matched, opcode, rules);
    if (!resolved.displaced)
      return;
    if (found && found->name != resolved.name)
      throw table_error(row.line, "an instruction may only be displaced through one base");
    found = resolved;
  };
  for (const auto &step: row.steps) {
    for (const auto &operand: step.operands)
      consider(operand);
    for (const auto &operand: step.destinations)
      consider(operand);
  }
  return found;
}

// Every vocabulary member the row names must be live: a `-` member is a hole,
// so the row does not cover that opcode even though the bits fit.
[[nodiscard]] constexpr bool members_live(
    const std::span<const Field> fields, const Row &row, const std::uint8_t opcode) {
  const auto live = [&](const Reference reference) { return !member_of(fields, reference, row.matched, opcode).hole; };
  const auto operands_live = [&](const auto &operands) {
    return std::ranges::all_of(operands,
        [&](const Operand &operand) { return operand.kind != Operand::Kind::Field || live(operand.reference); });
  };
  return std::ranges::all_of(row.pieces, [&](const Piece &piece) {
    return piece.kind != Piece::Kind::Field || live(piece.reference);
  }) && std::ranges::all_of(row.steps, [&](const Step &step) {
    return operands_live(step.operands) && operands_live(step.destinations) &&
           (!step.verb_reference || live(*step.verb_reference));
  });
}

// Earlier rows win, so a specific encoding must precede the general one that
// would otherwise swallow it: `halt` before `ld {r:y}, {r:z}`.
// A set of opcodes, as bits, so containment and overlap are four operations
// rather than 256.
struct OpcodeSet {
  std::array<std::uint64_t, 4> words{};

  constexpr void add(const std::uint8_t opcode) { words[opcode >> 6] |= std::uint64_t{1} << (opcode & 63); }
  [[nodiscard]] constexpr bool contains(const std::uint8_t opcode) const {
    return (words[opcode >> 6] >> (opcode & 63) & 1) != 0;
  }
  [[nodiscard]] constexpr bool empty() const {
    return std::ranges::all_of(words, [](const std::uint64_t word) { return word == 0; });
  }
  constexpr void add_all(const OpcodeSet &other) {
    for (std::size_t at = 0; at < words.size(); ++at)
      words[at] |= other.words[at];
  }
  [[nodiscard]] constexpr bool overlaps(const OpcodeSet &other) const {
    for (std::size_t at = 0; at < words.size(); ++at)
      if ((words[at] & other.words[at]) != 0)
        return true;
    return false;
  }
  // Every opcode of mine is also one of theirs: an override, rather than an accident.
  [[nodiscard]] constexpr bool within(const OpcodeSet &other) const {
    for (std::size_t at = 0; at < words.size(); ++at)
      if ((words[at] & ~other.words[at]) != 0)
        return false;
    return true;
  }
};

// A pattern *generates* its opcodes -- walk the cartesian product of its
// variable fields and place each combination -- rather than being tested
// against all 256. `BitSlice::place` exists for exactly this.
[[nodiscard]] constexpr OpcodeSet opcodes_of(const std::span<const Field> fields, const Row &row) {
  OpcodeSet result;
  std::size_t combinations = 1;
  for (const auto &slice: row.matched.slices)
    combinations *= std::size_t{slice.mask} + 1;
  for (std::size_t at = 0; at < combinations; ++at) {
    auto opcode = row.matched.opcode_bits;
    auto remaining = at;
    for (const auto &slice: row.matched.slices) {
      const auto values = std::size_t{slice.mask} + 1;
      opcode = static_cast<std::uint8_t>(opcode | slice.place(static_cast<std::uint8_t>(remaining % values)));
      remaining /= values;
    }
    if (members_live(fields, row, opcode))
      result.add(opcode);
  }
  return result;
}

// Line order silently decides who wins, so say what the legal shapes are: a row
// must win something, and where two rows overlap the earlier must be wholly
// contained in the later. That is an override. A partial overlap is an accident.
constexpr bool check_row_precedence(
    const std::span<const Row> rows, const std::span<const Field> fields, const std::size_t num_tables) {
  // What each row would cover on its own, and what it actually wins once the
  // rows before it have taken their share.
  std::vector<OpcodeSet> covers;
  for (const auto &row: rows)
    covers.push_back(opcodes_of(fields, row));
  std::vector<OpcodeSet> claimed(num_tables);

  for (std::size_t earlier = 0; earlier < rows.size(); ++earlier) {
    const auto &mine = covers[earlier];
    if (mine.empty())
      throw table_error(rows[earlier].line, "this row matches no opcode at all");
    auto &already = claimed[rows[earlier].table];
    if (mine.within(already))
      throw table_error(rows[earlier].line, "an earlier row shadows this one completely");
    already.add_all(mine);
    for (std::size_t later = earlier + 1; later < rows.size(); ++later) {
      if (rows[later].table != rows[earlier].table)
        continue;
      if (const auto &theirs = covers[later]; mine.overlaps(theirs) && !mine.within(theirs))
        throw table_error(rows[earlier].line, "this row overlaps a later one without being contained by it");
    }
  }
  return true;
}

// Which row, if any, each table decodes each opcode to. Earlier rows win; then
// a derived table takes from its parent whatever it did not claim itself.
// Declaration order resolves a chain, because a parent is always declared
// before its children.
template<std::size_t NumTables>
[[nodiscard]] constexpr auto decode_tables(const std::span<const Row> rows, const std::span<const OpcodeSet> opcodes,
    const std::span<const TableDecl> tables) {
  std::array<std::array<std::optional<std::size_t>, 256>, NumTables> all{};
  for (std::size_t index = 0; index < rows.size(); ++index)
    for (std::size_t opcode = 0; opcode < 256; ++opcode)
      if (opcodes[index].contains(static_cast<std::uint8_t>(opcode)) && !all[rows[index].table][opcode])
        all[rows[index].table][opcode] = index;
  for (std::size_t which = 0; which < tables.size(); ++which)
    if (tables[which].derived)
      for (std::size_t opcode = 0; opcode < 256; ++opcode)
        if (!all[which][opcode])
          all[which][opcode] = all[tables[which].parent][opcode];
  return all;
}

// Which tables are entered with a displacement already read. `dd cb d op` is
// the one encoding whose opcode is not its last byte, so the row that reads `d`
// hands it on rather than using it. Two consequences, both derived from the
// gotos that reach a table rather than declared on it: its rows use the
// displacement instead of reading one, and its opcode arrives by an operand
// read rather than an instruction fetch -- the machine has already committed,
// which is why the real chip does not increment R for that byte.
template<std::size_t NumTables>
[[nodiscard]] constexpr std::array<bool, NumTables> latched_tables(const std::span<const Row> rows) {
  std::array<bool, NumTables> latched{};
  std::array<bool, NumTables> seen{};
  for (const auto &row: rows)
    for (const auto &step: row.steps) {
      if (step.kind != Step::Kind::Goto)
        continue;
      if (seen[step.target] && latched[step.target] != row.reads_displacement)
        throw table_error(row.line, "this table is reached both with and without a displacement");
      seen[step.target] = true;
      latched[step.target] = row.reads_displacement;
    }
  return latched;
}

// Every opcode of every table must decode to something. On real hardware one
// always does -- an unassigned encoding still has an effect -- so a table that
// declines to say is an incomplete description rather than a permissive one. A
// catch-all row is how a table says "and everything else does this".
//
// Requiring it here is what lets the dispatch loop call without checking.
template<std::size_t NumTables>
constexpr bool check_tables_total(const std::span<const TableDecl> tables,
    const std::array<std::array<std::optional<std::size_t>, 256>, NumTables> &decoded) {
  for (std::size_t which = 0; which < tables.size(); ++which)
    for (std::size_t opcode = 0; opcode < 256; ++opcode)
      if (!decoded[which][opcode])
        throw table_error(tables[which].line, "table '" + std::string(tables[which].name) +
                                                  "' does not say what opcode " + decimal(opcode) +
                                                  " does; add a row, or `xxxxxxxx` last to catch the rest");
  return true;
}

// A rule rewrites `{field}` references and never literal text, which is what
// keeps `ex de, hl` right under a view. That silence also hides a mistake: a row
// that spells a renamed name out, and is inherited unchanged by the table that
// renames it, is almost certainly wrong. `ex (sp), hl` was, and decoded as
// `ex (sp), hl` under `dd` where the chip does `ex (sp), ix`.
//
// A row written *in* the derived table is exempt: putting it there is how one
// says the literal was meant.
template<std::size_t NumTables>
constexpr bool check_inherited_literals(const std::span<const Row> rows, const std::span<const TableDecl> tables,
    const std::array<std::array<std::optional<std::size_t>, 256>, NumTables> &decoded) {
  for (std::size_t which = 0; which < tables.size(); ++which) {
    if (!tables[which].derived)
      continue;
    for (std::size_t opcode = 0; opcode < 256; ++opcode) {
      const auto index = decoded[which][opcode];
      if (!index || rows[*index].table == which)
        continue;
      const auto &row = rows[*index];
      for (const auto &step: row.steps)
        for (const auto *operands: {&step.operands, &step.destinations})
          for (const auto &operand: *operands)
            if (operand.kind == Operand::Kind::Named)
              for (const auto &rule: tables[which].rules)
                if (rule.from == operand.name.view())
                  throw table_error(row.line,
                      "table '" + std::string(tables[which].name) + "' renames '" + std::string(rule.from) +
                          "', and this row names it literally where a rule cannot reach it; give that table its own "
                          "row, or name a vocabulary");
    }
  }
  return true;
}

// A table nothing reaches is never instantiated, so nothing in it is ever
// type-checked. An empty one is a typo.
constexpr bool check_tables_used(
    const std::span<const Row> rows, const std::span<const TableDecl> tables, const std::uint8_t entry) {
  for (std::size_t which = 0; which < tables.size(); ++which) {
    // A derived table with no rows of its own is its parent, renamed -- which is
    // the whole point of one.
    if (!tables[which].derived && std::ranges::none_of(rows, [&](const Row &row) { return row.table == which; }))
      throw table_error(tables[which].line, "this table has no rows");
    if (which == entry)
      continue;
    const auto reached = std::ranges::any_of(rows, [&](const Row &row) {
      return std::ranges::any_of(
          row.steps, [&](const Step &step) { return step.kind == Step::Kind::Goto && step.target == which; });
    });
    if (!reached)
      throw table_error(tables[which].line, "no goto reaches this table, so nothing in it is ever checked");
  }
  return true;
}

// The description this build was compiled against. Everything above parses
// whatever it is given; only these three name the embedded file.
inline constexpr auto fields = parse_fields<count_matching(cpu_description, &is_field)>(cpu_description);
inline constexpr auto tables = parse_tables<count_matching(cpu_description, &is_table)>(cpu_description, fields);
inline constexpr auto rows = parse_rows<count_matching(cpu_description, &is_row)>(cpu_description, fields, tables);

inline constexpr auto row_opcodes = [] {
  std::array<OpcodeSet, rows.size()> all{};
  for (std::size_t index = 0; index < rows.size(); ++index)
    all[index] = opcodes_of(fields, rows[index]);
  return all;
}();

inline constexpr auto decoded = decode_tables<tables.size()>(rows, row_opcodes, tables);
inline constexpr auto latched = latched_tables<tables.size()>(rows);

// Decoding starts in the first table declared; no name is special.
inline constexpr std::uint8_t entry_table = 0;

[[nodiscard]] constexpr std::optional<std::size_t> find_row(const std::uint8_t table, const std::uint8_t opcode) {
  return decoded[table][opcode];
}

// A row that only transfers elsewhere renders nothing: it is a prefix.
[[nodiscard]] constexpr std::optional<std::uint8_t> transfers_to(const Row &row) {
  if (row.steps.size() == 1 && row.steps[0].kind == Step::Kind::Goto)
    return row.steps[0].target;
  return std::nullopt;
}

inline constexpr std::size_t decoded_count = [] {
  std::size_t count = 0;
  for (const auto &table: decoded)
    count += static_cast<std::size_t>(std::ranges::count_if(table, &std::optional<std::size_t>::has_value));
  return count;
}();

static_assert(check_row_precedence(rows, fields, tables.size()));
static_assert(check_tables_used(rows, tables, entry_table));
static_assert(check_inherited_literals<tables.size()>(rows, tables, decoded));
static_assert(check_tables_total<tables.size()>(tables, decoded));

} // namespace specbolt::v4
