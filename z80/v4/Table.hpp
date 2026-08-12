#pragma once

#ifndef SPECBOLT_MODULES
#include "refract/Coverage.hpp"
#include "refract/Model.hpp"
#include "refract/Parse.hpp"

#include <array>
#include <optional>
#include <span>
#include <string_view>
#endif

// The description this build compiles, and the constants built from it. These
// live in the library's namespace because they *are* what it was given.
namespace specbolt::refract {

// clang-format off
inline constexpr char cpu_raw[] = {
#embed SPECBOLT_CPU_TABLE
  , 0
};
// clang-format on

inline constexpr std::string_view cpu_description{cpu_raw};

// The description this build was compiled against. Everything above parses
// whatever it is handed; these are where the embedded file enters. (Diagnostics
// name it too, through `SPECBOLT_CPU_TABLE` in TableError.hpp -- which is why
// one build can hold only one description.)
inline constexpr auto vocabularies =
    parse_vocabularies<count_matching(cpu_description, &is_vocabulary)>(cpu_description);
inline constexpr auto tables = parse_tables<count_matching(cpu_description, &is_table)>(cpu_description, vocabularies);
inline constexpr auto rows =
    parse_rows<count_matching(cpu_description, &is_row)>(cpu_description, vocabularies, tables);

inline constexpr auto row_opcodes = [] {
  std::array<OpcodeSet, rows.size()> all{};
  for (std::size_t index = 0; index < rows.size(); ++index)
    all[index] = opcodes_of(vocabularies, rows[index]);
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

static_assert(check_every_line_means_something(cpu_description));
static_assert(check_row_precedence(rows, row_opcodes, tables.size()));
static_assert(check_tables_used(rows, tables, entry_table));
static_assert(check_derived_rows_override(rows, row_opcodes, tables));
static_assert(check_displacement_rendered<tables.size()>(vocabularies, rows, tables, decoded));
static_assert(check_inherited_literals<tables.size()>(rows, tables, decoded));
static_assert(check_tables_total<tables.size()>(tables, decoded));

} // namespace specbolt::refract
