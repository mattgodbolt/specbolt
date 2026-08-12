#pragma once

#ifndef SPECBOLT_MODULES
#include "z80/v4/Coverage.hpp"
#include "z80/v4/Model.hpp"
#include "z80/v4/Parse.hpp"

#include <array>
#include <optional>
#include <span>
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

// The description this build was compiled against. Everything above parses
// whatever it is handed; these are where the embedded file enters. (Diagnostics
// name it too, through `SPECBOLT_CPU_TABLE` in TableError.hpp -- which is why
// one build can hold only one description.)
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

static_assert(check_every_line_means_something(cpu_description));
static_assert(check_row_precedence(rows, row_opcodes, tables.size()));
static_assert(check_tables_used(rows, tables, entry_table));
static_assert(check_inherited_literals<tables.size()>(rows, tables, decoded));
static_assert(check_tables_total<tables.size()>(tables, decoded));

} // namespace specbolt::v4
