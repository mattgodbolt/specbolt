#pragma once

// A description, compiled: the constants every consumer of a parsed `.cpu` file needs, made from its text during
// constant evaluation and checked there. `Compiled<Source>` hands out its parts, in the order the pipeline makes them:
//
//   vocabularies   the `vocab` lines, in declaration order
//   tables         the `table` lines, in declaration order
//   rows           every row of every table, in file order
//   decoded        per table, which row (an index into `rows`) each of its 256 opcodes decodes to, or nothing
//                  where no row claims it; the checks require every table to be total, so a consumer may
//                  dereference each
//   latched        per table, whether it is entered with a displacement byte already read, because the row that
//                  reached it read one before the opcode (CPU_FORMAT.md, "Latched tables")
//
// Instantiating the class runs every check in Checks.hpp over the description, so a malformed one is a compile error
// naming the file and line wherever it is first used.

#include "refract/Checks.hpp"
#include "refract/Decode.hpp"
#include "refract/Model.hpp"
#include "refract/Parse.hpp"
#include "refract/TableError.hpp"
#include "refract/ToArray.hpp"

#include <array>
#include <concepts>
#include <string_view>
#include <type_traits>

namespace specbolt::refract {

// Where a description comes from: its text, and the name diagnostics call it. A type rather than two template arguments
// because a `std::string_view` is not structural, so it cannot be a template argument itself, and a type carries both
// facts under one name:
//
//   struct MySource {
//     static constexpr std::string_view file = "my.cpu";
//     static constexpr std::string_view text = ...;   // the file's contents, usually by `#embed`
//   };
template<typename S>
concept SourceLike = requires {
  { S::file } -> std::convertible_to<std::string_view>;
  { S::text } -> std::convertible_to<std::string_view>;
  // Both must be usable in constant expressions, which is what naming them as template arguments asks.
  typename std::integral_constant<std::size_t, std::string_view{S::file}.size()>;
  typename std::integral_constant<std::size_t, std::string_view{S::text}.size()>;
};

// Each constant is the answer of one step of the pipeline; the size of each is whatever the text turned out to say.
namespace steps {

// One step of the pipeline: what `Step` returns, fixed by `to_array`. A step that rejects the text throws with the
// line, and `naming` adds the file.
template<SourceLike Source, std::regular_invocable auto Step>
inline constexpr auto fixed = to_array<[] { return naming(Source::file, Step); }>();

// The vocabularies the text declares, in declaration order. Every other part is made from these, so this step runs
// first whatever asks, and is where every line is checked for meaning something: a mistyped line is then reported at
// the typo rather than where something names it.
template<SourceLike Source>
inline constexpr auto vocabularies = fixed<Source, [] {
  check_every_line_means_something(Source::text);
  return parse_vocabularies(Source::text);
}>;

// The tables the text declares, in declaration order.
template<SourceLike Source>
inline constexpr auto tables = fixed<Source, [] { return parse_tables(Source::text, vocabularies<Source>); }>;

// Every row of every table, in the order the text writes them.
template<SourceLike Source>
inline constexpr auto rows =
    fixed<Source, [] { return parse_rows(Source::text, vocabularies<Source>, tables<Source>); }>;

// The opcodes each row claims, index-coupled to `rows`.
template<SourceLike Source>
inline constexpr auto row_opcodes = fixed<Source, [] { return opcodes_of_each(vocabularies<Source>, rows<Source>); }>;

// Per table, which row (as an index into `rows`) each of its 256 opcodes decodes to, or nothing where no row claims it.
template<SourceLike Source>
inline constexpr auto decoded =
    fixed<Source, [] { return decode_tables(rows<Source>, row_opcodes<Source>, tables<Source>); }>;

// Per table, whether it is entered with a displacement already read.
template<SourceLike Source>
inline constexpr auto latched = fixed<Source, [] { return latched_tables(rows<Source>, tables<Source>.size()); }>;

} // namespace steps

// The description a `Source` holds, compiled: each part of it as a constant, checked as a whole, with the lookups every
// consumer of it needs.
template<SourceLike Source>
struct Compiled {
  static constexpr std::string_view file = Source::file;
  static constexpr std::string_view text = Source::text;

  // The table decoding starts in: the first one declared, since the format reserves no name for the entry table.
  static constexpr std::uint8_t entry_table = 0;

  // The parts of the description, as constants. What each holds is set out at the top of this file.
  [[nodiscard]] static constexpr const auto &vocabularies() { return steps::vocabularies<Source>; }
  [[nodiscard]] static constexpr const auto &tables() { return steps::tables<Source>; }
  [[nodiscard]] static constexpr const auto &rows() { return steps::rows<Source>; }
  [[nodiscard]] static constexpr const auto &decoded() { return steps::decoded<Source>; }
  [[nodiscard]] static constexpr const auto &latched() { return steps::latched<Source>; }

  // The parts above as one `Description`: what a consumer that takes one, such as the disassembler, is handed. An
  // interpreter's handlers are templates on the parts themselves, so they reach them through the functions above.
  [[nodiscard]] static constexpr Description description() {
    return {vocabularies(), rows(), tables(), decoded(), entry_table};
  }

  // The rules the whole description must obey, run once when this class is instantiated: each throws against its line,
  // `naming` puts the file in front, and that is the compile error. The block may call the members above because this
  // is a class template, so it runs at instantiation, when their bodies exist; in a plain class it could not. `latched`
  // is asked for, by a check of its shape, because deriving it is itself a check and nothing else forces it.
  consteval {
    static_assert(latched().size() == tables().size());
    naming(file, [] {
      check_row_precedence(description(), steps::row_opcodes<Source>);
      check_derived_rows_override(description(), steps::row_opcodes<Source>);
      check_tables_used(description());
      check_tables_total(description());
      check_inherited_literals(description());
      check_displacement_rendered(description());
    });
  }
};

} // namespace specbolt::refract
