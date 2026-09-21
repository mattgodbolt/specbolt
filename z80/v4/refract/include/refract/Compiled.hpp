#pragma once

// A description, compiled: the constants every consumer of a parsed `.cpu`
// file needs, made from its text during constant evaluation and checked there.
//
// `Text` is the description and `File` is what diagnostics call it. Asking
// this for its `description()`, or running `check()`, on a malformed
// description is a compile error naming the file and line; instantiating the
// class alone evaluates nothing.

#include "refract/Coverage.hpp"
#include "refract/Model.hpp"
#include "refract/Parse.hpp"
#include "refract/TableError.hpp"
#include "refract/ToArray.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace specbolt::refract {

// Each constant is the answer of one step of the pipeline, fixed by
// `to_array`; the size of each is whatever the text turned out to say. A step
// that rejects the text throws with the line, and `naming` adds the file.
namespace steps {

// The vocabularies the text declares, in declaration order.
template<const std::string_view &Text, FileName File>
inline constexpr auto vocabularies =
    to_array<[] { return naming(File.view(), [] { return parse_vocabularies(Text); }); }>();

// The tables the text declares, in declaration order.
template<const std::string_view &Text, FileName File>
inline constexpr auto tables =
    to_array<[] { return naming(File.view(), [] { return parse_tables(Text, vocabularies<Text, File>); }); }>();

// Every row of every table, in the order the text writes them.
template<const std::string_view &Text, FileName File>
inline constexpr auto rows = to_array<[] {
  return naming(File.view(), [] { return parse_rows(Text, vocabularies<Text, File>, tables<Text, File>); });
}>();

// The opcodes each row claims, index-coupled to `rows`.
template<const std::string_view &Text, FileName File>
inline constexpr auto row_opcodes = to_array<[] {
  return naming(File.view(), [] { return opcodes_of_each(vocabularies<Text, File>, rows<Text, File>); });
}>();

// Per table, which row (as an index into `rows`) each of its 256 opcodes
// decodes to, or nothing where no row claims it.
template<const std::string_view &Text, FileName File>
inline constexpr auto decoded = to_array<[] {
  return naming(
      File.view(), [] { return decode_tables(rows<Text, File>, row_opcodes<Text, File>, tables<Text, File>); });
}>();

// Per table, whether it is entered with a displacement already read.
template<const std::string_view &Text, FileName File>
inline constexpr auto latched = to_array<[] {
  return naming(File.view(), [] { return latched_tables(rows<Text, File>, tables<Text, File>.size()); });
}>();

} // namespace steps

// The description `Text`, compiled: each part of it as a constant, the checks
// on the whole, and the lookups every consumer of it needs.
template<const std::string_view &Text, FileName File>
struct Compiled {
  static constexpr std::string_view file = File.view();
  static constexpr std::string_view text = Text;

  // The table decoding starts in: the first one declared, since the format
  // reserves no name for the entry table.
  static constexpr std::uint8_t entry_table = 0;

  // One check, run with the file put in front of whatever it throws. A check
  // throws against its line or returns true.
  template<auto Check>
  static constexpr bool checked = naming(file, Check);

  // The parts of the description, each evaluated on first use, as
  // `description()` and `check()` are: the vocabularies and tables as
  // declared, the rows in file order, then per table which row (an index into
  // `rows()`) each opcode decodes to, and whether the table is entered with a
  // displacement already read.
  [[nodiscard]] static constexpr const auto &vocabularies() { return steps::vocabularies<Text, File>; }
  [[nodiscard]] static constexpr const auto &tables() { return steps::tables<Text, File>; }
  [[nodiscard]] static constexpr const auto &rows() { return steps::rows<Text, File>; }
  [[nodiscard]] static constexpr const auto &decoded() { return steps::decoded<Text, File>; }
  [[nodiscard]] static constexpr const auto &latched() { return steps::latched<Text, File>; }

  // Runs every check the text must pass, each against its line, and returns
  // true if all do; a failing one throws, which makes it a compile error.
  // `description()` asserts it, and so does an interpreter.
  [[nodiscard]] static consteval bool check() {
    return checked<[] { return check_every_line_means_something(text); }> &&
           checked<[] { return check_row_precedence(unchecked(), steps::row_opcodes<Text, File>); }> &&
           checked<[] { return check_derived_rows_override(unchecked(), steps::row_opcodes<Text, File>); }> &&
           checked<[] { return check_tables_used(unchecked()); }> &&
           checked<[] { return check_tables_total(unchecked()); }> &&
           checked<[] { return check_inherited_literals(unchecked()); }> &&
           checked<[] { return check_displacement_rendered(unchecked()); }>;
  }

  // The parts above as one `Description`, its checks passed: what a consumer
  // that takes a `Description`, such as the disassembler, is handed. An
  // interpreter's handlers are templates on the parts themselves, so they
  // reach them through the functions above and call `check()` on their own.
  [[nodiscard]] static constexpr Description description() {
    static_assert(check());
    return unchecked();
  }

  // The index into `rows()` of the row that decodes `opcode` in `table`, or
  // nothing if no row does. Every table is total once `check()` has passed, so
  // a consumer that has run it may dereference the answer.
  [[nodiscard]] static constexpr std::optional<std::size_t> find_row(
      const std::uint8_t table, const std::uint8_t opcode) {
    return decoded()[table][opcode];
  }

private:
  // The parts as one `Description` without asserting the checks: what the
  // checks themselves are run against.
  [[nodiscard]] static constexpr Description unchecked() {
    return {vocabularies(), rows(), tables(), decoded(), entry_table};
  }
};

} // namespace specbolt::refract
