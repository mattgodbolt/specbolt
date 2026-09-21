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

template<const std::string_view &Text, FileName File>
inline constexpr auto vocabularies =
    to_array<[] { return naming(File.view(), [] { return parse_vocabularies(Text); }); }>();

template<const std::string_view &Text, FileName File>
inline constexpr auto tables =
    to_array<[] { return naming(File.view(), [] { return parse_tables(Text, vocabularies<Text, File>); }); }>();

template<const std::string_view &Text, FileName File>
inline constexpr auto rows = to_array<[] {
  return naming(File.view(), [] { return parse_rows(Text, vocabularies<Text, File>, tables<Text, File>); });
}>();

template<const std::string_view &Text, FileName File>
inline constexpr auto row_opcodes = to_array<[] {
  return naming(File.view(), [] { return opcodes_of_each(vocabularies<Text, File>, rows<Text, File>); });
}>();

template<const std::string_view &Text, FileName File>
inline constexpr auto decoded = to_array<[] {
  return naming(
      File.view(), [] { return decode_tables(rows<Text, File>, row_opcodes<Text, File>, tables<Text, File>); });
}>();

template<const std::string_view &Text, FileName File>
inline constexpr auto latched = to_array<[] {
  return naming(File.view(), [] { return latched_tables(rows<Text, File>, tables<Text, File>.size()); });
}>();

} // namespace steps

template<const std::string_view &Text, FileName File>
struct Compiled {
  static constexpr std::string_view file = File.view();
  static constexpr std::string_view text = Text;

  // Decoding starts in the first table declared: the format reserves no name
  // for the entry table.
  static constexpr std::uint8_t entry_table = 0;

  // The checks the text must pass. Each throws against its line or returns
  // true; `naming` puts the file in front of whatever it throws.
  template<auto Check>
  static constexpr bool checked = naming(file, Check);

  // Each step, evaluated on first use, as `description()` and `check()` are.
  [[nodiscard]] static constexpr const auto &vocabularies() { return steps::vocabularies<Text, File>; }
  [[nodiscard]] static constexpr const auto &tables() { return steps::tables<Text, File>; }
  [[nodiscard]] static constexpr const auto &rows() { return steps::rows<Text, File>; }
  [[nodiscard]] static constexpr const auto &decoded() { return steps::decoded<Text, File>; }
  [[nodiscard]] static constexpr const auto &latched() { return steps::latched<Text, File>; }

  // Every check the text must pass, each against its line; true if all do.
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

  // The above as one value, checked: what a consumer that takes a
  // `Description`, such as the disassembler, is handed. An interpreter's
  // handlers are templates on the parts themselves, so they reach them through
  // the functions above and call `check()` on their own.
  [[nodiscard]] static constexpr Description description() {
    static_assert(check());
    return unchecked();
  }

  [[nodiscard]] static constexpr std::optional<std::size_t> find_row(
      const std::uint8_t table, const std::uint8_t opcode) {
    return decoded()[table][opcode];
  }

private:
  [[nodiscard]] static constexpr Description unchecked() {
    return {vocabularies(), rows(), tables(), decoded(), entry_table};
  }
};

} // namespace specbolt::refract
