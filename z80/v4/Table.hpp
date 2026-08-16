#pragma once

#include "refract/Coverage.hpp"
#include "refract/Model.hpp"
#include "refract/Parse.hpp"
#include "refract/ToArray.hpp"

#include <array>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace specbolt::v4 {

using namespace refract;

// clang-format off
inline constexpr char cpu_raw[] = {
#embed SPECBOLT_CPU_TABLE
};
// clang-format on

// `#embed` already knows how long the file is, so the view is built from the
// size rather than from a terminator the description would otherwise have to
// carry.
inline constexpr std::string_view cpu_description{cpu_raw, sizeof cpu_raw};

// The description this build was compiled against. Everything above parses
// whatever it is handed; these are where the embedded file enters. (Diagnostics
// name it too, through `SPECBOLT_CPU_TABLE` in TableError.hpp, which is why
// one build can hold only one description.)
//
// This is also the whole of the boundary between compile time and run time.
// Each step works in `std::vector` and `to_array` fixes the answer; the size of
// each one is whatever the description turned out to say. Five of the six are
// read by running code: the disassembler walks `rows`, `vocabularies` and
// `tables`, `find_row` reads `decoded`, and the dispatch loop reads `latched`.
// `row_opcodes` is the exception: only the checks below want it, and it is a
// constant so that three of them share one computation.
inline constexpr auto vocabularies = to_array<[] { return parse_vocabularies(cpu_description); }>();
inline constexpr auto tables = to_array<[] { return parse_tables(cpu_description, vocabularies); }>();
inline constexpr auto rows = to_array<[] { return parse_rows(cpu_description, vocabularies, tables); }>();
inline constexpr auto row_opcodes = to_array<[] { return opcodes_of_each(vocabularies, rows); }>();
inline constexpr auto decoded = to_array<[] { return decode_tables(rows, row_opcodes, tables); }>();
inline constexpr auto latched = to_array<[] { return latched_tables(rows, tables.size()); }>();

// Decoding starts in the first table declared; no name is special.
inline constexpr std::uint8_t entry_table = 0;

// The above as one value, for anything that wants the table rather than its
// parts. The disassembler is handed this and needs nothing else from here.
inline constexpr Description description{vocabularies, rows, tables, decoded, entry_table};

[[nodiscard]] constexpr std::optional<std::size_t> find_row(const std::uint8_t table, const std::uint8_t opcode) {
  return decoded[table][opcode];
}

static_assert(check_every_line_means_something(cpu_description));
static_assert(check_row_precedence(description, row_opcodes));
static_assert(check_derived_rows_override(description, row_opcodes));
static_assert(check_tables_used(description));
static_assert(check_tables_total(description));
static_assert(check_inherited_literals(description));
static_assert(check_displacement_rendered(description));

} // namespace specbolt::v4
