#include "z80/Generated.hpp"

#include <catch2/catch_test_macros.hpp>

namespace specbolt {

TEST_CASE("Opcode generation tests") {
  SECTION("Test base opcode disassembly") {
    const auto &names = base_opcode_names();
    CHECK(names[0x00] == "nop");
    CHECK(names[0x01] == "ld bc, nnnn");
  }
  SECTION("Test base opcode operations") {
    const auto &ops = base_opcode_ops();
    CHECK(ops[0]);
  }
}

} // namespace specbolt
