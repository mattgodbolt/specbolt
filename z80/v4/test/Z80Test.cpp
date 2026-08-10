#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <iostream>
#include <ranges>

#ifdef SPECBOLT_MODULES
import z80_v4;
#else
#include "z80/v4/Z80.hpp"
#endif

namespace specbolt::v4 {
TEST_CASE("TODO tests") {
  Scheduler scheduler;
  Memory memory{4};
  Z80 z80{scheduler, memory};
  CHECK(z80.test() == 1234);
}
} // namespace specbolt::v4
