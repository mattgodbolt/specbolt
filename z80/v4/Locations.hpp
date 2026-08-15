#pragma once

// Where a row's names for storage are looked up.
//
// The registers come from `RegisterFile`; the rest are the enums `Z80.hpp`
// declares alongside the `read` and `write` overloads that reach them. All this
// file adds is the scopes reflection is to search when a row says `a` or
// `carry` or `pc`.

#include "z80/common/RegisterFile.hpp"
#include "z80/v4/Z80.hpp"

#include <meta>
#include <vector>

namespace specbolt::v4 {

// Where the table may name storage locations from. A `std::vector` because only
// `consteval` code ever reads it: the list is walked inside the same constant
// evaluation that builds it, so nothing has to outlive that.
//
// The registers are named outright because they live in the shared register
// file; everything else is *every enum this namespace declares*, because that
// is what those enums are for. `Z80.hpp` declares one per family of locations
// precisely so that a name can be found by walking `enumerators_of`. Writing
// the list out instead means a new enum silently is not a location. Nothing
// here has to exclude the enums that are not locations either: a name that
// resolved to two things is `only_match`'s diagnostic, so a collision would be
// named rather than guessed at.
//
// `members_of` sees what has been declared by the time it is *evaluated*, which
// is during the first instantiation of `execute_one`, long after every header
// is in. A scope declared later than that would not be found.
[[nodiscard]] consteval std::vector<std::meta::info> location_scopes() {
  std::vector scopes{^^RegisterFile::R8, ^^RegisterFile::R16};
  for (const auto member: std::meta::members_of(^^specbolt::v4::locations, std::meta::access_context::current()))
    if (std::meta::is_type(member) && std::meta::is_enum_type(member))
      scopes.push_back(member);
  return scopes;
}

} // namespace specbolt::v4
