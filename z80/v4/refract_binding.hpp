#pragma once

// What `refract` is generating for, in this build.
//
// The library includes this by name and nothing else from its consumer, so it
// is the whole of the coupling in that direction. It does not put anything in
// the library's namespace: it points the library at a namespace of its own,
// which is where the machine and the table already live.
//
// A second description would be a second one of these — and also a second
// binary, because the table constants and `Cpu` are definitions rather than
// parameters. That is the honest limit of this arrangement.

#include "Table.hpp"
#include "Z80Machine.hpp"

namespace specbolt::refract {

// Where this build's machine and table are to be found.
namespace target = ::specbolt::v4;

} // namespace specbolt::refract
