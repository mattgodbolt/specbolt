#pragma once

// The three kinds of declaration a description contains, each reading the whole text and returning what it found. Each
// returns a `std::vector`: nothing here knows how many of anything a description holds, and nothing has to. See
// ToArray.hpp for where that becomes a size.

#include "refract/Lexical.hpp"
#include "refract/Model.hpp"
#include "refract/Parser.hpp"
#include "refract/Pattern.hpp"
#include "refract/TableError.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace specbolt::refract {

// How many vocabularies, and how many tables, a description may declare. Both are held as a `std::uint8_t` wherever one
// is referred to, which is what bounds them rather than any judgement about how many a description needs.
inline constexpr std::size_t max_vocabularies = 256;
inline constexpr std::size_t max_tables = 256;

// Parses every `vocab name [: scope] = member member...` line of the text, in the order they appear.
[[nodiscard]] constexpr std::vector<Vocabulary> parse_vocabularies(const std::string_view description) {
  std::vector<Vocabulary> result;
  for (const auto [at, text]: lines_of(description)) {
    at_line(at, [&] {
      if (!is_vocabulary(text))
        return;
      Parser parser(text);
      parser.skip_word();
      Vocabulary vocabulary;
      vocabulary.line = at;
      vocabulary.name = parser.next_word();
      if (vocabulary.name.empty())
        throw std::runtime_error("vocabulary declaration has no name");
      auto next = parser.next_word();
      // A `:` clause names the scope this vocabulary's members are looked up in, rather than every location the CPU
      // offers.
      if (next == ":") {
        vocabulary.scope = parser.next_word();
        if (vocabulary.scope.empty())
          throw std::runtime_error("':' introduces the scope a vocabulary's members come from, and none was given");
        // Checked here rather than where it becomes a `Name`, which is inside `resolve`, long after anyone could act
        // on it.
        if (vocabulary.scope.size() > Name::capacity)
          throw std::runtime_error("scope name '" + std::string(vocabulary.scope) + "' is too long");
        next = parser.next_word();
      }
      if (next != "=")
        throw std::runtime_error("expected '=' in vocabulary declaration");
      while (!parser.eof()) {
        const auto value = parser.next_word();
        if (value.empty())
          continue;
        vocabulary.members.push_back(parse_member(value));
      }
      if (vocabulary.members.empty())
        throw std::runtime_error("vocabulary declares no members");
      if (std::ranges::contains(result, vocabulary.name, &Vocabulary::name))
        throw std::runtime_error("duplicate vocabulary name");
      // A reference holds its vocabulary as a byte, so this is a real capacity like the rest, and says so rather than
      // wrapping.
      if (result.size() == max_vocabularies)
        throw std::runtime_error("too many vocabularies");
      result.push_back(vocabulary);
    });
  }
  return result;
}

// Checks that every line is blank, a comment, a declaration or a row. A line that is none of those is a mistyped one of
// them (a row that lost its separators, or `vocabularies` for `vocab`) and would otherwise be skipped in silence,
// surfacing much later as an opcode nothing decodes.
constexpr void check_every_line_means_something(const std::string_view description) {
  for (const auto [at, text]: lines_of(description)) {
    at_line(at, [&] {
      if (text.empty() || text.front() == '#' || is_vocabulary(text) || is_table(text) || is_row(text))
        return;
      throw std::runtime_error("this is not a comment, a declaration, or a row; a row needs its '|' separators");
    });
  }
}

// The index into `vocabularies` of the one called `name`, or nothing if none is.
[[nodiscard]] constexpr std::optional<std::size_t> find_vocabulary(
    const std::span<const Vocabulary> vocabularies, const std::string_view name) {
  const auto found = std::ranges::find(vocabularies, name, &Vocabulary::name);
  if (found == vocabularies.end())
    return std::nullopt;
  return static_cast<std::size_t>(found - vocabularies.begin());
}

// The bare name in a table declaration's first word: `t(view:v)` declares a table called `t`, the parenthesised part
// being its view. Both the declaration and the scan for a table's rows need the bare name.
[[nodiscard]] constexpr std::string_view table_name_of(const std::string_view word) {
  const auto open = word.find('(');
  return open == std::string_view::npos ? word : word.substr(0, open);
}

[[nodiscard]] constexpr Reference reference_from_braces(
    std::span<const Vocabulary> vocabularies, std::string_view text, const Pattern &matched, const TableDecl &table);

// Parses a derived table's substitution list into `table.rules`: `vocabulary.member -> replacement`, comma separated,
// with either spacing. A right side written as a view reference substitutes whichever member the table's view selects,
// as the Z80's `pair.hl -> {index:view}` does.
constexpr void parse_substitutions(
    const std::string_view text, const std::span<const Vocabulary> vocabularies, TableDecl &table) {
  Parser list(text);
  while (!list.eof()) {
    const auto rule = list.next_field(',');
    if (rule.empty())
      continue;
    const auto arrow = rule.find("->");
    if (arrow == std::string_view::npos)
      throw std::runtime_error("expected '->' in table substitution '" + std::string(rule) + "'");
    const auto left = Parser::trim(rule.substr(0, arrow));
    const auto to = Parser::trim(rule.substr(arrow + 2));
    if (left.empty() || to.empty())
      throw std::runtime_error("a table substitution needs a name on each side of '->'");
    const auto dot = left.find('.');
    if (dot == std::string_view::npos)
      throw std::runtime_error(
          "a table substitution names the vocabulary it rewrites, as in 'vocabulary.member -> replacement'");
    const auto vocabulary = left.substr(0, dot);
    const auto named = find_vocabulary(vocabularies, vocabulary);
    if (!named)
      throw std::runtime_error("substitution names a vocabulary that does not exist");
    const auto from = left.substr(dot + 1);
    if (from.empty())
      throw std::runtime_error("a table substitution needs a name on each side of '->'");
    if (!std::ranges::contains(vocabularies[*named].members, from, &Member::display))
      throw std::runtime_error(
          "vocabulary '" + std::string(vocabulary) + "' has no member '" + std::string(from) + "'");
    if (from == "-")
      throw std::runtime_error("a hole is not a member; a substitution cannot rename one");
    // The generated code reads such a vocabulary's value straight out of the opcode, so a rule renaming one of its
    // members would be honoured by the disassembler and ignored by the interpreter.
    if (is_numeric(vocabularies[*named]))
      throw std::runtime_error("vocabulary '" + std::string(vocabulary) +
                               "' is its own slice, its members being the numbers the opcode carries, so a "
                               "substitution cannot rename one");
    Rule substitution{.vocabulary_index = static_cast<std::uint8_t>(*named), .from = from};
    if (to.starts_with('{')) {
      if (!table.takes_view())
        throw std::runtime_error("only a table that takes a view may substitute a view reference");
      const auto reference = reference_from_braces(vocabularies, to, Pattern{}, table);
      if (!reference.from_view)
        throw std::runtime_error("a substitution's reference must be selected by the table's view");
      substitution.to_is_view = true;
      substitution.to_vocabulary = reference.vocabulary_index;
    }
    else {
      // What a row claims is worked out before any rule is applied, so a row renamed to nothing would still claim its
      // opcodes and then resolve to a default zero.
      substitution.to = parse_member(to);
      if (substitution.to.hole)
        throw std::runtime_error("a substitution cannot rename something to nothing; a hole belongs in a vocabulary");
    }
    table.rules.push_back(substitution);
  }
}

// Parses every `table name[(view:vocabulary)] [= parent with substitutions]` line of the text, in the order they
// appear.
[[nodiscard]] constexpr std::vector<TableDecl> parse_tables(
    const std::string_view description, const std::span<const Vocabulary> vocabularies) {
  std::vector<TableDecl> result;
  for (const auto [at, text]: lines_of(description)) {
    at_line(at, [&] {
      if (!is_table(text))
        return;
      Parser parser(text);
      parser.skip_word();
      auto name = parser.next_word();
      if (name.empty())
        throw std::runtime_error("table declaration has no name");
      TableDecl table{.line = at};
      // `t(view:v)`: the table is decoded once per member of `v`, and a row writes `view` where a slice letter would
      // go.
      if (const auto open = name.find('('); open != std::string_view::npos) {
        if (!name.ends_with(')'))
          throw std::runtime_error("unterminated '(' in table view");
        Parser inner(name.substr(open + 1, name.size() - open - 2));
        table.view_name = inner.take_until(':');
        const auto vocabulary = inner.rest();
        if (table.view_name.empty() || vocabulary.empty())
          throw std::runtime_error("a table view names itself and a vocabulary, as in 'name(view:vocabulary)'");
        const auto named = find_vocabulary(vocabularies, vocabulary);
        if (!named)
          throw std::runtime_error("table view names a vocabulary that does not exist");
        table.view_vocabulary = static_cast<std::uint8_t>(*named);
        name = name.substr(0, open);
      }
      table.name = name;
      if (std::ranges::contains(result, name, &TableDecl::name))
        throw std::runtime_error("duplicate table name");
      if (const auto equals = parser.next_word(); !equals.empty()) {
        if (equals != "=")
          throw std::runtime_error("expected '= <parent> with <substitutions>' after the table name");
        // Only a table already declared, which makes the derivation a forest: a parent's own rows are resolved before
        // anything inherits them. `result` holds exactly those, this one not being in it yet.
        const auto parent = parser.next_word();
        const auto found = std::ranges::find(result, parent, &TableDecl::name);
        if (found == result.end())
          throw std::runtime_error("no table named '" + std::string(parent) + "' is declared above this one");
        table.derived = true;
        table.parent = static_cast<std::uint8_t>(found - result.begin());
        if (parser.next_word() != "with")
          throw std::runtime_error("expected 'with' after the parent table name");
        parse_substitutions(parser.rest(), vocabularies, table);
        if (table.rules.empty())
          throw std::runtime_error("a derived table declares no substitutions, so it is its parent");
      }
      // A goto holds its target as a byte, as a row holds the table it is in.
      if (result.size() == max_tables)
        throw std::runtime_error("too many tables");
      result.push_back(table);
    });
  }
  return result;
}

// The index into `tables` of the one called `name`; an error if there is none.
[[nodiscard]] constexpr std::uint8_t find_table(const std::span<const TableDecl> tables, const std::string_view name) {
  const auto found = std::ranges::find(tables, name, &TableDecl::name);
  if (found == tables.end())
    throw std::runtime_error("no table named '" + std::string(name) + "'");
  return static_cast<std::uint8_t>(found - tables.begin());
}

// The index into `matched.slices` of the slice lettered `name`, or nothing if the pattern has no such slice.
[[nodiscard]] constexpr std::optional<std::size_t> find_slice(const Pattern &matched, const char name) {
  const auto found = std::ranges::find(matched.slices, name, &BitSlice::name);
  if (found == matched.slices.end())
    return std::nullopt;
  return static_cast<std::size_t>(found - matched.slices.begin());
}

// Checks that every member of a vocabulary a view selects shares one shape, as `shape_of` below defines it, and brings
// no operation. A view is chosen by a prefix at run time, so every compile-time check resolves such a reference at
// member 0 and trusts the answer for all of them (`displaced_through` takes no view at all); a member that differed
// would run one addressing mode while printing another.
//
// Reported against the line that selects it, which made it a requirement, naming the declaration, which is the line to
// edit: a vocabulary nothing selects by a view may hold whatever it likes.
constexpr void check_view_vocabulary(const Vocabulary &vocabulary) {
  const auto &first = vocabulary.members[0];
  const auto shape_of = [](const Member &member) {
    return std::tuple{member.hole, member.operand.indirect, member.operand.displaced, member.operand.write_back_delay,
        member.operand.kind, member.pieces.size()};
  };
  const auto complaint = [&](const std::string_view what, const std::string_view because) {
    return std::runtime_error("vocabulary '" + std::string(vocabulary.name) + "' (declared at line " +
                              decimal(vocabulary.line) + ") is selected by a view here, so " + std::string(because) +
                              "; '" + std::string(what) + "' does not");
  };
  for (const auto &member: vocabulary.members) {
    // The operation is spliced from member 0, so a member that brought its own would be ignored for every view but the
    // first: a member a view selects names a location and nothing else.
    if (!member.operation.empty())
      throw complaint(member.display, "each of its members may only name a location, since a view is chosen long "
                                      "after the operation has been spliced");
    if (shape_of(member) != shape_of(first))
      throw complaint(
          member.display, "all of its members must have the same shape as '" + std::string(first.display) + "'");
    if (!std::ranges::equal(member.pieces, first.pieces, {}, &Piece::kind, &Piece::kind))
      throw complaint(
          member.display, "all of its members must render the same way as '" + std::string(first.display) + "'");
  }
}

// Parses the inside of a `{...}` reference: `vocabulary:slice` binds a vocabulary to a slice of the opcode; in a table
// that takes a view, `vocabulary:view` binds it to the view instead, which the opcode does not carry and a prefix
// chose.
[[nodiscard]] constexpr Reference parse_reference(const std::span<const Vocabulary> vocabularies,
    const std::string_view inner, const Pattern &matched, const TableDecl &table) {
  Parser parser(inner);
  const auto name = parser.take_until(':');
  const auto slice = parser.rest();
  if (name.empty() || slice.empty())
    throw std::runtime_error("a reference names a vocabulary and one slice letter, as in {vocabulary:s}");
  const auto field = find_vocabulary(vocabularies, name);
  if (!field)
    throw std::runtime_error("reference names a vocabulary that does not exist");
  if (table.takes_view() && slice == table.view_name) {
    // A view is matched by name before a slice is looked for, so a view named like a slice letter would take every
    // reference meant for the opcode's bits, and take them in silence: the bits would be read by nothing and the prefix
    // would answer for all of them. Neither reading is obviously right, so neither is chosen.
    if (slice.size() == 1 && find_slice(matched, slice.front()))
      throw std::runtime_error("'" + std::string(slice) +
                               "' is this table's view and also a slice of this opcode, so this reference could "
                               "mean either; rename one of them");
    if (vocabularies[*field].members.size() != vocabularies[table.view_vocabulary].members.size())
      throw std::runtime_error("vocabulary has a different number of members than the table's view");
    check_view_vocabulary(vocabularies[*field]);
    return {.vocabulary_index = static_cast<std::uint8_t>(*field), .from_view = true};
  }
  if (slice.size() != 1)
    throw std::runtime_error("a reference names a vocabulary and one slice letter, as in {vocabulary:s}");
  const auto found = find_slice(matched, slice.front());
  if (!found)
    throw std::runtime_error("reference names a slice the opcode pattern does not define");
  if (vocabularies[*field].members.size() != std::size_t{matched.slices[*found].mask} + 1)
    throw std::runtime_error("vocabulary has the wrong number of members for its opcode bits");
  return {.vocabulary_index = static_cast<std::uint8_t>(*field), .slice_index = static_cast<std::uint8_t>(*found)};
}

// Parses a `{vocabulary:slice}` reference, braces included.
[[nodiscard]] constexpr Reference reference_from_braces(const std::span<const Vocabulary> vocabularies,
    const std::string_view text, const Pattern &matched, const TableDecl &table) {
  if (!text.starts_with('{') || !text.ends_with('}'))
    throw std::runtime_error("a reference names a vocabulary and one slice letter, as in {vocabulary:s}");
  return parse_reference(vocabularies, text.substr(1, text.size() - 2), matched, table);
}

// Parses one operand of a step: a `{...}` vocabulary reference, or anything `parse_simple_operand` accepts, either with
// an optional `name=` in front saying which parameter it feeds.
[[nodiscard]] constexpr Operand parse_operand(const std::span<const Vocabulary> vocabularies,
    const std::string_view word, const Pattern &matched, const std::uint8_t immediate_bytes, const TableDecl &table) {
  const auto [parameter, text] = split_keyword(word);
  auto operand = text.starts_with('{')
                     ? Operand{{}, Operand::Kind::Vocabulary, reference_from_braces(vocabularies, text, matched, table)}
                     : parse_simple_operand(text, immediate_bytes);
  operand.parameter = parameter;
  return operand;
}

// Splits the row's mnemonic into `row.pieces`: literal text, the values it renders from the encoding, and its
// vocabulary references.
constexpr void lower_mnemonic(const std::span<const Vocabulary> vocabularies, Row &row, const TableDecl &table) {
  const auto add_text = [&](const Parser text) {
    for (const auto &piece: pieces_of(text))
      row.pieces.push_back(piece);
  };

  Parser parser(row.mnemonic);
  while (!parser.eof()) {
    if (!parser.rest().contains('{')) {
      add_text(parser);
      return;
    }
    add_text(Parser(parser.take_until('{')));
    if (!parser.rest().contains('}'))
      throw std::runtime_error("unterminated vocabulary reference in mnemonic");
    row.pieces.push_back({.kind = Piece::Kind::Vocabulary,
        .reference = parse_reference(vocabularies, parser.take_until('}'), row.matched, table)});
  }
}

// Checks that a row's three columns agree about its immediate: the encoding says what is fetched, the mnemonic must
// render exactly that, and the action must use it, or one of the three is lying.
constexpr void check_immediates(const Row &row) {
  // A row fetches one immediate, of `immediate_bytes` bytes, so the mnemonic must render exactly one, of exactly that
  // width. Summing widths would let `$nn $nn` pass against `n n` and then disassemble as two bytes where the machine
  // read one sixteen-bit value.
  std::size_t rendered = 0;
  std::size_t width = 0;
  for (const auto &piece: row.pieces)
    switch (piece.kind) {
      case Piece::Kind::Imm8:
      case Piece::Kind::Relative:
        ++rendered;
        width = 1;
        break;
      case Piece::Kind::Imm16:
        ++rendered;
        width = 2;
        break;
      default: break;
    }
  if (rendered > 1)
    throw std::runtime_error("a row renders at most one immediate; the encoding only fetches one");
  if (width != row.immediate_bytes)
    throw std::runtime_error("the mnemonic renders a different number of immediate bytes than the encoding fetches");

  const auto immediate = [](const Operand &operand) { return operand.kind == Operand::Kind::Immediate; };
  const auto uses_immediate = std::ranges::any_of(row.steps, [&](const Step &step) {
    return std::ranges::any_of(step.operands, immediate) || std::ranges::any_of(step.destinations, immediate);
  });
  if (uses_immediate != (row.immediate_bytes != 0))
    throw std::runtime_error("the action and the encoding disagree about whether there is an immediate");
}

// Parses the first column into `row`: the opcode pattern, then whichever bytes the instruction carries after it.
constexpr void parse_encoding(Parser encoding, Row &row) {
  row.matched = parse_pattern(encoding.next_word());
  while (!encoding.eof()) {
    const auto token = encoding.next_word();
    if (token.empty())
      continue;
    if (token == "d") {
      if (row.reads_displacement)
        throw std::runtime_error("a row reads at most one displacement");
      row.reads_displacement = true;
      continue;
    }
    if (token != "n")
      throw std::runtime_error("'" + std::string(token) + "' is not an encoding byte; expected 'n' or 'd'");
    ++row.immediate_bytes;
  }
  if (row.immediate_bytes > 2)
    throw std::runtime_error("an instruction may carry at most two immediate bytes");
}

// Parses one step of the third column: an operation, the destinations written before `<-`, and the operands after it.
// `if` guards the rest of the row; `goto` hands decoding to another table and does nothing else.
[[nodiscard]] constexpr Step parse_step(Parser action, const std::span<const Vocabulary> vocabularies,
    const std::span<const TableDecl> tables, const Row &row, const TableDecl &table) {
  Step step{.operation = action.next_word()};
  if (step.operation == "if") {
    step.kind = Step::Kind::If;
    step.operation = action.next_word();
    if (step.operation.empty())
      throw std::runtime_error("'if' needs something to test");
  }
  if (step.operation == "goto") {
    if (step.kind == Step::Kind::If)
      throw std::runtime_error("a goto is the whole of its row, so it cannot be conditional; a row that decides "
                               "between two tables has to be two rows, one per encoding");
    step.kind = Step::Kind::Goto;
    auto destination = action.next_word();
    std::string_view supplied;
    if (const auto open = destination.find('('); open != std::string_view::npos) {
      if (!destination.ends_with(')'))
        throw std::runtime_error("unterminated '(' in goto");
      supplied = destination.substr(open + 1, destination.size() - open - 2);
      destination = destination.substr(0, open);
    }
    step.target = find_table(tables, destination);
    if (!action.next_word().empty())
      throw std::runtime_error("goto takes a single table name");
    const auto &target = tables[step.target];
    if (target.takes_view() && supplied.empty())
      throw std::runtime_error("this table takes a view, so the goto must say which");
    if (!target.takes_view() && !supplied.empty())
      throw std::runtime_error("this table takes no view, so the goto may not supply one");
    if (!supplied.empty()) {
      if (table.takes_view() && supplied == table.view_name) {
        if (table.view_vocabulary != target.view_vocabulary)
          throw std::runtime_error("the view being handed on is drawn from a different vocabulary");
        step.forwards_view = true;
      }
      else {
        const auto &members = vocabularies[target.view_vocabulary].members;
        const auto found = std::ranges::find(members, supplied, &Member::display);
        if (found == members.end())
          throw std::runtime_error("goto names a view that is not a member of that table's view vocabulary");
        step.target_view = static_cast<std::uint8_t>(found - members.begin());
      }
    }
    return step;
  }
  if (step.operation.starts_with('{'))
    step.operation_reference = reference_from_braces(vocabularies, step.operation, row.matched, table);
  // `operation dest <- args...`; the destination is optional
  auto writing_destination = action.rest().contains("<-");
  while (!action.eof()) {
    const auto word = trim_comma(action.next_word());
    if (word.empty())
      continue;
    if (word == "<-") {
      if (!writing_destination)
        throw std::runtime_error("a step has one '<-', between its destinations and its operands");
      writing_destination = false;
      continue;
    }
    const auto operand = parse_operand(vocabularies, word, row.matched, row.immediate_bytes, table);
    if (writing_destination) {
      if (!operand.indirect && (operand.kind == Operand::Kind::Constant || operand.kind == Operand::Kind::Immediate))
        throw std::runtime_error("'" + std::string(word) +
                                 "' is a value, not somewhere a result can go; a "
                                 "destination is a location, or an address in parentheses");
      if (!operand.parameter.empty())
        throw std::runtime_error("'" + std::string(operand.parameter.view()) +
                                 "=' names a parameter, and a destination is not one: it is where the result "
                                 "goes, not something handed to the operation");
      step.destinations.push_back(operand);
    }
    else {
      if (operand.kind == Operand::Kind::Discard)
        throw std::runtime_error("'-' discards a result, so it can only be a destination");
      step.operands.push_back(operand);
    }
  }
  return step;
}

// Parses every row of the text, `encoding | mnemonic | step ; step...`, in the order they appear. A row belongs to the
// nearest `table` line above it, which is why this pass tracks the current table where the vocabulary and table passes
// read the whole file flat.
[[nodiscard]] constexpr std::vector<Row> parse_rows(const std::string_view description,
    const std::span<const Vocabulary> vocabularies, const std::span<const TableDecl> tables) {
  std::vector<Row> result;
  std::optional<std::uint8_t> current;
  for (const auto [at, text]: lines_of(description)) {
    at_line(at, [&] {
      if (is_table(text)) {
        Parser declaration(text);
        declaration.skip_word();
        current = find_table(tables, table_name_of(declaration.next_word()));
        return;
      }
      if (!is_row(text))
        return;
      if (!current)
        throw std::runtime_error("this row is not in any table; declare one with `table <name>` first");
      // Three columns, separated by `|`: what is encoded, how it reads, what it does.
      Parser parser(text);
      Row row{.table = *current, .line = at};
      parse_encoding(Parser(parser.next_field('|')), row);
      row.mnemonic = parser.next_field('|');
      if (parser.rest().contains('|'))
        throw std::runtime_error("a row has three columns; a fourth '|' is one too many");
      // Steps run in order, separated by `;`.
      Parser sequence(Parser::trim(parser.rest()));
      while (!sequence.eof()) {
        Parser action(sequence.next_field(';'));
        if (action.eof())
          continue;
        row.steps.push_back(parse_step(action, vocabularies, tables, row, tables[*current]));
      }
      if (row.steps.empty())
        throw std::runtime_error("row has no action");
      // The disassembler renders nothing for a goto row and stops, so a goto has to be the whole row or the two would
      // disagree about what an opcode means.
      if (std::ranges::any_of(row.steps, [](const Step &step) { return step.kind == Step::Kind::Goto; }) &&
          row.steps.size() != 1) // NOLINT
        throw std::runtime_error("a goto is the whole of its row, so it cannot share one with another step");
      lower_mnemonic(vocabularies, row, tables[*current]);
      check_immediates(row);
      result.push_back(row);
    });
  }
  return result;
}

} // namespace specbolt::refract
