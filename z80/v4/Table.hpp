#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/v4/Matched.hpp"
#include "z80/v4/Parser.hpp"
#include "z80/v4/TableError.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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
  std::uint8_t field_index{};
  std::uint8_t slice_index{};
  bool indirect{};
  constexpr bool operator==(const Operand &) const = default;
};

struct Member {
  std::string_view display{};
  std::string_view primitive{};
  std::optional<Operand> appended{};
  bool hole{};
};

struct Reference {
  std::uint8_t field_index{};
  std::uint8_t slice_index{};
};

struct Field {
  static constexpr std::size_t max_values = 8;
  char name{};
  std::array<Member, max_values> values{};
  std::size_t num_values{};
};

struct Piece {
  enum class Kind : std::uint8_t { Literal, Field, Imm8, Imm16 };
  Kind kind{};
  std::string_view text{};
  std::uint8_t field_index{};
  std::uint8_t slice_index{};
};

inline constexpr std::size_t max_operands = 4;

// One application of one primitive, or a transfer into another decoding table.
// A row is an ordered list of these, which is where cost lives: an internal
// delay is a step like any other.
struct Step {
  enum class Kind : std::uint8_t { Apply, Goto };
  Kind kind{};
  std::uint8_t target{};
  std::string_view verb{};
  std::optional<Reference> verb_reference{};
  std::array<Operand, max_operands> destinations{};
  std::size_t num_destinations{};
  std::array<Operand, max_operands> operands{};
  std::size_t num_operands{};
};

struct Row {
  static constexpr std::size_t max_pieces = 12;
  static constexpr std::size_t max_steps = 6;
  Matched matched{};
  std::string_view mnemonic{};
  std::array<Piece, max_pieces> pieces{};
  std::size_t num_pieces{};
  std::uint8_t immediate_bytes{};
  std::size_t length{1};
  std::uint8_t table{};
  std::array<Step, max_steps> steps{};
  std::size_t num_steps{};
  std::size_t line{};
};

[[nodiscard]] constexpr std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    text.remove_suffix(1);
  return text;
}

[[nodiscard]] constexpr std::string_view trim_comma(std::string_view text) {
  if (text.ends_with(','))
    text.remove_suffix(1);
  return text;
}

[[nodiscard]] constexpr std::string_view next_word(Parser &parser) {
  parser.skip_any(" \t");
  return trim(parser.split_to(' ').data());
}

[[nodiscard]] constexpr bool is_field(const std::string_view line) { return line.starts_with("field "); }
[[nodiscard]] constexpr bool is_table(const std::string_view line) { return line.starts_with("table "); }
[[nodiscard]] constexpr bool is_row(const std::string_view line) {
  return !line.empty() && line.front() != '#' && !is_field(line) && !is_table(line) && line.contains('|');
}

[[nodiscard]] consteval std::size_t count_matching(bool (*predicate)(std::string_view)) {
  Parser parser(cpu_description);
  std::size_t count = 0;
  while (!parser.eof())
    if (predicate(trim(parser.split_to('\n').data())))
      ++count;
  return count;
}

[[nodiscard]] consteval Operand parse_simple_operand(std::string_view word, const std::size_t line) {
  if (word.empty())
    throw table_error(line, "empty operand in action");
  if (word == "-")
    return {Operand::Kind::Discard, {}, 0, 0, 0, 0, false};
  if (word.starts_with('(')) {
    if (!word.ends_with(')'))
      throw table_error(line, "unterminated '(' in operand '" + std::string(word) + "'");
    word = word.substr(1, word.size() - 2);
    auto addressed = parse_simple_operand(word, line);
    if (addressed.indirect)
      throw table_error(line, "an address cannot itself be indirect");
    addressed.indirect = true;
    return addressed;
  }
  if (word == "n")
    return {Operand::Kind::Immediate, {}, 0, 0, 0, 0, false};
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
    return {Operand::Kind::Constant, {}, static_cast<std::uint16_t>(value), 0, 0, 0, false};
  }
  if (word.size() > Name::capacity)
    throw table_error(line, "operand name '" + std::string(word) + "' is too long");
  return {Operand::Kind::Named, Name{word}, 0, 0, 0, 0, false};
}

// `bc` is display only; `adc:add8+carry` binds to a primitive and appends an
// operand the encoding does not carry.
[[nodiscard]] consteval Member parse_member(const std::string_view text, const std::size_t line) {
  Parser parser(text);
  Member member{parser.split_to(':').data(), parser.data(), std::nullopt, false};
  if (member.display.empty())
    throw table_error(line, "field member has no name");
  if (member.display == "-") {
    member.hole = true;
    return member;
  }
  Parser primitive(member.primitive);
  member.primitive = primitive.split_to('+').data();
  if (const auto appended = primitive.data(); !appended.empty()) {
    member.appended = parse_simple_operand(appended, line);
    if (member.appended->kind == Operand::Kind::Immediate)
      throw table_error(line, "a vocabulary member cannot append an immediate; only the encoding fetches those");
  }
  return member;
}

template<std::size_t N>
[[nodiscard]] consteval std::array<Field, N> parse_fields() {
  std::array<Field, N> result{};
  Parser lines(cpu_description);
  std::size_t index = 0;
  while (!lines.eof()) {
    const auto at = lines.line();
    const auto text = trim(lines.split_to('\n').data());
    if (!is_field(text))
      continue;
    Parser parser(text);
    static_cast<void>(next_word(parser));
    auto &field = result[index++];
    const auto name = next_word(parser);
    if (name.size() != 1)
      throw table_error(at, "field name must be a single character");
    field.name = name.front();
    if (next_word(parser) != "=")
      throw table_error(at, "expected '=' in field declaration");
    while (!parser.eof()) {
      const auto value = next_word(parser);
      if (value.empty())
        continue;
      if (field.num_values == Field::max_values)
        throw table_error(at, "too many values in field");
      field.values[field.num_values++] = parse_member(value, at);
    }
    if (field.num_values == 0)
      throw table_error(at, "field declares no values");
    for (std::size_t other = 0; other + 1 < index; ++other)
      if (result[other].name == field.name)
        throw table_error(at, "duplicate field name");
  }
  return result;
}

inline constexpr auto fields = parse_fields<count_matching(&is_field)>();

template<std::size_t N>
[[nodiscard]] consteval std::array<std::string_view, N> parse_tables() {
  std::array<std::string_view, N> result{};
  Parser lines(cpu_description);
  std::size_t index = 0;
  while (!lines.eof()) {
    const auto at = lines.line();
    const auto text = trim(lines.split_to('\n').data());
    if (!is_table(text))
      continue;
    Parser parser(text);
    static_cast<void>(next_word(parser));
    const auto name = next_word(parser);
    if (name.empty())
      throw table_error(at, "table declaration has no name");
    if (!next_word(parser).empty())
      throw table_error(at, "table declaration takes a single name");
    for (std::size_t other = 0; other < index; ++other)
      if (result[other] == name)
        throw table_error(at, "duplicate table name");
    result[index++] = name;
  }
  return result;
}

inline constexpr auto tables = parse_tables<count_matching(&is_table)>();

[[nodiscard]] consteval std::uint8_t find_table(const std::string_view name, const std::size_t line) {
  for (std::size_t index = 0; index < tables.size(); ++index)
    if (tables[index] == name)
      return static_cast<std::uint8_t>(index);
  throw table_error(line, "no table named '" + std::string(name) + "'");
}

[[nodiscard]] constexpr std::optional<std::size_t> find_field(const char name) {
  for (std::size_t index = 0; index < fields.size(); ++index)
    if (fields[index].name == name)
      return index;
  return std::nullopt;
}

[[nodiscard]] constexpr std::optional<std::size_t> find_slice(const Matched &matched, const char name) {
  for (std::size_t index = 0; index < matched.num_slices; ++index)
    if (matched.slices[index].name == name)
      return index;
  return std::nullopt;
}

// `{p}` names one letter for both; `{r:z}` binds vocabulary r to slice z.
[[nodiscard]] consteval Reference parse_reference(
    const std::string_view inner, const Matched &matched, const std::size_t line) {
  Parser parser(inner);
  const auto vocabulary = parser.split_to(':').data();
  const auto slice = parser.eof() ? vocabulary : parser.data();
  if (vocabulary.size() != 1 || slice.size() != 1)
    throw table_error(line, "reference must be {x} or {vocabulary:slice}");
  const auto field = find_field(vocabulary.front());
  if (!field)
    throw table_error(line, "reference names a vocabulary that does not exist");
  const auto found = find_slice(matched, slice.front());
  if (!found)
    throw table_error(line, "reference names a field the opcode pattern does not define");
  if (fields[*field].num_values != std::size_t{matched.slices[*found].mask} + 1)
    throw table_error(line, "vocabulary has the wrong number of values for its opcode bits");
  return {static_cast<std::uint8_t>(*field), static_cast<std::uint8_t>(*found)};
}

[[nodiscard]] consteval Reference reference_from_braces(
    const std::string_view text, const Matched &matched, const std::size_t line) {
  if (!text.starts_with('{') || !text.ends_with('}'))
    throw table_error(line, "reference must be {x} or {vocabulary:slice}");
  return parse_reference(text.substr(1, text.size() - 2), matched, line);
}

[[nodiscard]] consteval Operand parse_operand(
    const std::string_view word, const Matched &matched, const std::size_t line) {
  if (!word.starts_with('{'))
    return parse_simple_operand(word, line);
  const auto reference = reference_from_braces(word, matched, line);
  return {Operand::Kind::Field, {}, 0, 0, reference.field_index, reference.slice_index, false};
}

consteval void lower_mnemonic(Row &row) {
  const auto push = [&row](const Piece piece) {
    if (row.num_pieces == Row::max_pieces)
      throw table_error(row.line, "mnemonic is too complicated");
    row.pieces[row.num_pieces++] = piece;
  };
  const auto push_text = [&](Parser text) {
    while (!text.eof()) {
      if (!text.data().contains('$')) {
        push({Piece::Kind::Literal, text.data(), 0, 0});
        return;
      }
      if (const auto literal = text.split_to('$').data(); !literal.empty())
        push({Piece::Kind::Literal, literal, 0, 0});
      const auto remaining = text.data().size();
      text.skip_any("n");
      switch (remaining - text.data().size()) {
        case 2: push({Piece::Kind::Imm8, {}, 0, 0}); break;
        case 4: push({Piece::Kind::Imm16, {}, 0, 0}); break;
        default: throw table_error(row.line, "expected $nn or $nnnn in mnemonic");
      }
    }
  };

  Parser parser(row.mnemonic);
  while (!parser.eof()) {
    if (!parser.data().contains('{')) {
      push_text(parser);
      return;
    }
    push_text(Parser(parser.split_to('{').data()));
    if (!parser.data().contains('}'))
      throw table_error(row.line, "unterminated field reference in mnemonic");
    const auto reference = parse_reference(parser.split_to('}').data(), row.matched, row.line);
    push({Piece::Kind::Field, {}, reference.field_index, reference.slice_index});
  }
}

// The encoding says what is fetched. The mnemonic must render exactly that, and
// the action must use it: otherwise one of the three columns is lying.
consteval void check_immediates(const Row &row) {
  std::size_t rendered = 0;
  for (const auto &piece: std::span{row.pieces.data(), row.num_pieces})
    rendered += piece.kind == Piece::Kind::Imm8 ? 1u : piece.kind == Piece::Kind::Imm16 ? 2u : 0u;
  if (rendered != row.immediate_bytes)
    throw table_error(row.line, "the mnemonic renders a different number of immediate bytes than the encoding fetches");

  const auto uses_immediate = std::ranges::any_of(std::span{row.steps.data(), row.num_steps}, [](const Step &step) {
    return std::ranges::any_of(std::span{step.operands.data(), step.num_operands},
        [](const Operand &operand) { return operand.kind == Operand::Kind::Immediate; });
  });
  if (uses_immediate != (row.immediate_bytes != 0))
    throw table_error(row.line, "the action and the encoding disagree about whether there is an immediate");
}

template<std::size_t N>
[[nodiscard]] consteval std::array<Row, N> parse_rows() {
  std::array<Row, N> result{};
  Parser lines(cpu_description);
  std::size_t index = 0;
  std::optional<std::uint8_t> current;
  while (!lines.eof()) {
    const auto at = lines.line();
    const auto text = trim(lines.split_to('\n').data());
    if (is_table(text)) {
      Parser declaration(text);
      static_cast<void>(next_word(declaration));
      current = find_table(next_word(declaration), at);
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
    Parser encoding(trim(parser.split_to('|').data()), at);
    row.matched = parse_opcode_bits(next_word(encoding), at);
    while (!encoding.eof()) {
      const auto token = next_word(encoding);
      if (token.empty())
        continue;
      if (token != "n")
        throw table_error(at, "'" + std::string(token) + "' is not an encoding byte; expected 'n'");
      ++row.immediate_bytes;
    }
    if (row.immediate_bytes > 2)
      throw table_error(at, "an instruction may carry at most two immediate bytes");
    row.length = 1u + row.immediate_bytes;
    row.mnemonic = trim(parser.split_to('|').data());
    // steps run in order, separated by `;`
    Parser sequence(trim(parser.data()));
    while (!sequence.eof()) {
      Parser action(trim(sequence.split_to(';').data()));
      if (action.eof())
        continue;
      if (row.num_steps == Row::max_steps)
        throw table_error(at, "row has too many steps");
      auto &step = row.steps[row.num_steps++];
      step.verb = next_word(action);
      if (step.verb == "goto") {
        step.kind = Step::Kind::Goto;
        step.target = find_table(next_word(action), at);
        if (!next_word(action).empty())
          throw table_error(at, "goto takes a single table name");
        continue;
      }
      if (step.verb.starts_with('{'))
        step.verb_reference = reference_from_braces(step.verb, row.matched, at);
      // `verb dest <- args...`; the destination is optional
      auto writing_destination = action.data().contains("<-");
      while (!action.eof()) {
        const auto word = trim_comma(next_word(action));
        if (word.empty())
          continue;
        if (word == "<-") {
          writing_destination = false;
          continue;
        }
        const auto operand = parse_operand(trim_comma(word), row.matched, at);
        if (writing_destination) {
          if (step.num_destinations == max_operands)
            throw table_error(at, "too many destinations");
          step.destinations[step.num_destinations++] = operand;
        }
        else {
          if (operand.kind == Operand::Kind::Discard)
            throw table_error(at, "'-' discards a result, so it can only be a destination");
          if (step.num_operands == max_operands)
            throw table_error(at, "too many operands");
          step.operands[step.num_operands++] = operand;
        }
      }
    }
    if (row.num_steps == 0)
      throw table_error(at, "row has no action");
    lower_mnemonic(row);
    for (auto &step: std::span{row.steps.data(), row.num_steps})
      for (auto &operand: std::span{step.operands.data(), step.num_operands})
        if (operand.kind == Operand::Kind::Immediate)
          operand.width = row.immediate_bytes;
    check_immediates(row);
  }
  return result;
}

inline constexpr auto rows = parse_rows<count_matching(&is_row)>();

// A field operand names whichever vocabulary member its slice selects, and that
// member is written the same way an operand is written in a row.
[[nodiscard]] consteval Operand resolve(
    const Operand operand, const Matched &matched, const std::uint8_t opcode, const std::size_t line) {
  if (operand.kind != Operand::Kind::Field)
    return operand;
  return parse_simple_operand(
      fields[operand.field_index].values[matched.slices[operand.slice_index].extract(opcode)].display, line);
}

// A row matches only if the bits fit AND every vocabulary member it names is live:
// a `-` member is a hole, so the row simply does not cover that opcode.
[[nodiscard]] constexpr bool row_matches(const Row &row, const std::uint8_t opcode) {
  if (!row.matched.matches(opcode))
    return false;
  const auto live = [&](const Reference reference) {
    return !fields[reference.field_index].values[row.matched.slices[reference.slice_index].extract(opcode)].hole;
  };
  const auto operands_live = [&](const std::span<const Operand> operands) {
    return std::ranges::all_of(operands, [&](const Operand &operand) {
      return operand.kind != Operand::Kind::Field || live({operand.field_index, operand.slice_index});
    });
  };
  return std::ranges::all_of(std::span{row.pieces.data(), row.num_pieces}, [&](const Piece &piece) {
    return piece.kind != Piece::Kind::Field || live({piece.field_index, piece.slice_index});
  }) && std::ranges::all_of(std::span{row.steps.data(), row.num_steps}, [&](const Step &step) {
    return operands_live({step.operands.data(), step.num_operands}) &&
           operands_live({step.destinations.data(), step.num_destinations}) &&
           (!step.verb_reference || live(*step.verb_reference));
  });
}

// Earlier rows win, so a specific encoding must precede the general one that
// would otherwise swallow it: `halt` before `ld {r:y}, {r:z}`.
inline constexpr auto decoded = [] {
  std::array<std::array<std::optional<std::size_t>, 256>, tables.size()> all{};
  for (std::size_t which = 0; which < all.size(); ++which)
    for (std::size_t opcode = 0; opcode < all[which].size(); ++opcode)
      for (std::size_t index = 0; index < rows.size(); ++index)
        if (rows[index].table == which && row_matches(rows[index], static_cast<std::uint8_t>(opcode))) {
          all[which][opcode] = index;
          break;
        }
  return all;
}();

// Decoding starts in the first table declared; no name is special.
inline constexpr std::uint8_t entry_table = 0;

[[nodiscard]] constexpr std::optional<std::size_t> find_row(const std::uint8_t table, const std::uint8_t opcode) {
  return decoded[table][opcode];
}

// A row that only transfers elsewhere renders nothing: it is a prefix.
[[nodiscard]] constexpr std::optional<std::uint8_t> transfers_to(const Row &row) {
  if (row.num_steps == 1 && row.steps[0].kind == Step::Kind::Goto)
    return row.steps[0].target;
  return std::nullopt;
}

inline constexpr std::size_t decoded_count = [] {
  std::size_t count = 0;
  for (const auto &table: decoded)
    count += static_cast<std::size_t>(std::ranges::count_if(table, &std::optional<std::size_t>::has_value));
  return count;
}();

[[nodiscard]] consteval std::array<bool, 256> opcodes_matching(const Row &row) {
  std::array<bool, 256> result{};
  for (std::size_t opcode = 0; opcode < result.size(); ++opcode)
    result[opcode] = row_matches(row, static_cast<std::uint8_t>(opcode));
  return result;
}

// Line order silently decides who wins, so say what the legal shapes are: a row
// must win something, and where two rows overlap the earlier must be wholly
// contained in the later. That is an override. A partial overlap is an accident.
consteval bool check_row_precedence() {
  for (std::size_t earlier = 0; earlier < rows.size(); ++earlier) {
    const auto mine = opcodes_matching(rows[earlier]);
    if (std::ranges::none_of(mine, std::identity{}))
      throw table_error(rows[earlier].line, "this row matches no opcode at all");
    bool wins = false;
    for (std::size_t opcode = 0; opcode < mine.size(); ++opcode)
      if (mine[opcode] && decoded[rows[earlier].table][opcode] == earlier)
        wins = true;
    if (!wins)
      throw table_error(rows[earlier].line, "an earlier row shadows this one completely");
    for (std::size_t later = earlier + 1; later < rows.size(); ++later) {
      if (rows[later].table != rows[earlier].table)
        continue;
      const auto theirs = opcodes_matching(rows[later]);
      bool shared = false;
      bool escapes = false;
      for (std::size_t opcode = 0; opcode < mine.size(); ++opcode) {
        shared = shared || (mine[opcode] && theirs[opcode]);
        escapes = escapes || (mine[opcode] && !theirs[opcode]);
      }
      if (shared && escapes)
        throw table_error(rows[earlier].line, "this row overlaps a later one without being contained by it");
    }
  }
  return true;
}

static_assert(check_row_precedence());

// Handlers are generated by instantiating one table from another, so a cycle
// would not terminate. Prefix chains need one, which is what the loop model in
// NOTES is for; until then, say so plainly.
consteval bool check_no_goto_cycles() {
  for (std::size_t start = 0; start < tables.size(); ++start) {
    std::array<bool, 64> seen{};
    std::size_t at = start;
    for (std::size_t depth = 0; depth <= tables.size(); ++depth) {
      if (seen[at])
        throw table_error(0, "tables goto each other in a cycle, which cannot be unrolled");
      seen[at] = true;
      bool moved = false;
      for (const auto &row: rows)
        if (row.table == at)
          for (const auto &step: std::span{row.steps.data(), row.num_steps})
            if (step.kind == Step::Kind::Goto && !moved) {
              at = step.target;
              moved = true;
            }
      if (!moved)
        break;
    }
  }
  return true;
}

static_assert(check_no_goto_cycles());

[[nodiscard]] constexpr std::uint8_t field_value(const Row &row, const char name, const std::uint8_t opcode) {
  const auto slice = find_slice(row.matched, name);
  if (!slice)
    throw table_error(row.line, "row references a field the opcode pattern does not define");
  return row.matched.slices[*slice].extract(opcode);
}

} // namespace specbolt::v4
