#pragma once

// A description, compiled: the constants every consumer of a parsed `.cpu` file needs, made from its text during
// constant evaluation and checked there. `Compiled<Source>` hands out five parts, in the order the pipeline makes them:
//
//   vocabularies   the `vocab` lines, in declaration order
//   tables         the `table` lines, in declaration order
//   rows           every row of every table, in file order
//   decoded        per table, which row (an index into `rows`) each of its 256 opcodes decodes to, or nothing
//   latched        per table, whether it is entered with a displacement byte already read, because the row that
//                  reached it read one before the opcode (CPU_FORMAT.md, "Latched tables")
//
// Asking for `description()`, or running `check()`, on a malformed description is a compile error naming the file and
// line; instantiating the class alone evaluates nothing.

#include "refract/Checks.hpp"
#include "refract/Decode.hpp"
#include "refract/Model.hpp"
#include "refract/Parse.hpp"
#include "refract/TableError.hpp"
#include "refract/ToArray.hpp"

#include <array>
#include <concepts>
#include <optional>
#include <string_view>

namespace specbolt::refract {

// Where a description comes from: its text, and the name diagnostics call it. A type rather than two template
// arguments because a `std::string_view` is not structural, so it cannot be a template argument itself, and a type
// carries both facts under one name:
//
//   struct Z80Source {
//     static constexpr std::string_view file = "z80.cpu";
//     static constexpr std::string_view text = ...;   // the file's contents, usually by `#embed`
//   };
template<typename S>
concept SourceLike = requires {
  { S::file } -> std::convertible_to<std::string_view>;
  { S::text } -> std::convertible_to<std::string_view>;
};

// Each constant is the answer of one step of the pipeline, fixed by `to_array`; the size of each is whatever the text
// turned out to say. A step that rejects the text throws with the line, and `naming` adds the file.
namespace steps {

// The vocabularies the text declares, in declaration order.
template<SourceLike Source>
inline constexpr auto vocabularies =
    to_array<[] { return naming(Source::file, [] { return parse_vocabularies(Source::text); }); }>();

// The tables the text declares, in declaration order.
template<SourceLike Source>
inline constexpr auto tables =
    to_array<[] { return naming(Source::file, [] { return parse_tables(Source::text, vocabularies<Source>); }); }>();

// Every row of every table, in the order the text writes them.
template<SourceLike Source>
inline constexpr auto rows = to_array<[] {
  return naming(Source::file, [] { return parse_rows(Source::text, vocabularies<Source>, tables<Source>); });
}>();

// The opcodes each row claims, index-coupled to `rows`.
template<SourceLike Source>
inline constexpr auto row_opcodes =
    to_array<[] { return naming(Source::file, [] { return opcodes_of_each(vocabularies<Source>, rows<Source>); }); }>();

// Per table, which row (as an index into `rows`) each of its 256 opcodes decodes to, or nothing where no row claims it.
template<SourceLike Source>
inline constexpr auto decoded = to_array<[] {
  return naming(Source::file, [] { return decode_tables(rows<Source>, row_opcodes<Source>, tables<Source>); });
}>();

// Per table, whether it is entered with a displacement already read.
template<SourceLike Source>
inline constexpr auto latched =
    to_array<[] { return naming(Source::file, [] { return latched_tables(rows<Source>, tables<Source>.size()); }); }>();

} // namespace steps

// The description a `Source` holds, compiled: each part of it as a constant, the checks on the whole, and the lookups
// every consumer of it needs.
template<SourceLike Source>
struct Compiled {
  static constexpr std::string_view file = Source::file;
  static constexpr std::string_view text = Source::text;

  // The table decoding starts in: the first one declared, since the format reserves no name for the entry table.
  static constexpr std::uint8_t entry_table = 0;

  // One check, run with the file put in front of whatever it throws. A check throws against its line or returns true.
  template<std::predicate auto Check>
  static constexpr bool checked = naming(file, Check);

  // The parts of the description, each evaluated on first use, as `description()` and `check()` are. What each holds
  // is set out at the top of this file.
  [[nodiscard]] static constexpr const auto &vocabularies() { return steps::vocabularies<Source>; }
  [[nodiscard]] static constexpr const auto &tables() { return steps::tables<Source>; }
  [[nodiscard]] static constexpr const auto &rows() { return steps::rows<Source>; }
  [[nodiscard]] static constexpr const auto &decoded() { return steps::decoded<Source>; }
  [[nodiscard]] static constexpr const auto &latched() { return steps::latched<Source>; }

  // Runs every check the text must pass, each against its line, and returns true if all do; a failing one throws,
  // which makes it a compile error. `description()` asserts it, and so does an interpreter.
  static consteval bool check() {
    return checked<[] { return check_every_line_means_something(text); }> &&
           checked<[] { return check_row_precedence(unchecked(), steps::row_opcodes<Source>); }> &&
           checked<[] { return check_derived_rows_override(unchecked(), steps::row_opcodes<Source>); }> &&
           checked<[] { return check_tables_used(unchecked()); }> &&
           checked<[] { return check_tables_total(unchecked()); }> &&
           checked<[] { return check_inherited_literals(unchecked()); }> &&
           checked<[] { return check_displacement_rendered(unchecked()); }>;
  }

  // The parts above as one `Description`, its checks passed: what a consumer that takes a `Description`, such as the
  // disassembler, is handed. An interpreter's handlers are templates on the parts themselves, so they reach them
  // through the functions above and call `check()` on their own.
  [[nodiscard]] static constexpr Description description() {
    consteval { check(); }
    return unchecked();
  }

  // The index into `rows()` of the row that decodes `opcode` in `table`, or nothing if no row does. Every table is
  // total once `check()` has passed, so a consumer that has run it may dereference the answer.
  [[nodiscard]] static constexpr std::optional<std::size_t> find_row(
      const std::uint8_t table, const std::uint8_t opcode) {
    return decoded()[table][opcode];
  }

private:
  // The parts as one `Description` without asserting the checks: what the checks themselves are run against.
  [[nodiscard]] static constexpr Description unchecked() {
    return {vocabularies(), rows(), tables(), decoded(), entry_table};
  }
};

} // namespace specbolt::refract
