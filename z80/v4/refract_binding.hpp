#pragma once

// What `refract` is generating for, in this build: a machine, and a table.
//
// The library includes this by name and nothing else from its consumer, so it
// is the whole of the coupling in that direction. A second description would
// be a second one of these — which is also why one binary can hold only one:
// both the table constants and `Cpu` are single definitions, not parameters.

#include "Table.hpp"
#include "Z80Cpu.hpp"
