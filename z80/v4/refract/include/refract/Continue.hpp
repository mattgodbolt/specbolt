#pragma once

// What a machine's conditions return. Its own header, because a CPU's palette of operations needs this and nothing else
// of refract's.

namespace specbolt::refract {

// What a condition answers: whether the rest of its row runs. An operation that returns one is a condition, and a row
// applies it like any other step; `Continue::no` abandons the steps after it. Nothing in the row says a step is a
// condition, because the operation's signature does. A type of its own rather than `bool`, because a `bool` is already
// a value (the Z80's flag bits are read as one), and a value belongs in a destination.
enum class Continue : bool { no, yes };

// `Continue::yes` if `condition` holds: what a condition returns.
[[nodiscard]] constexpr Continue continue_if(const bool condition) { return condition ? Continue::yes : Continue::no; }

} // namespace specbolt::refract
