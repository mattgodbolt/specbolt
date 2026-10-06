#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <meta>
#include <string_view>
#include <vector>

#include "refract/Compiled.hpp"
#include "refract/Execute.hpp"
#include "refract/Model.hpp"

// A machine that names its accessors as it pleases, run end to end: the generated code reaches each location through
// the member the machine marks for it, whatever that is called. A machine whose accessors were called `read` and
// `write` could not tell that from calling them by name. This one also has a location it can write and not read, and a
// table whose view selects between locations.

namespace specbolt::refract {
namespace {

enum class Reg : std::uint8_t { a, x };
// A location the machine is told and never asked, as a port is.
enum class Port : std::uint8_t { out };

struct Renamed {
  std::array<std::uint8_t, 0x100> program{};
  std::uint8_t pc{};
  std::uint8_t a{};
  std::uint8_t x{};
  std::vector<std::uint8_t> sent{};
  std::size_t instructions_left{};

  bool start_instruction() {
    if (instructions_left == 0)
      return false;
    --instructions_left;
    return true;
  }
  [[nodiscard]] std::uint8_t fetch_opcode() { return program[pc++]; }
  [[nodiscard]] std::uint8_t fetch_immediate() { return program[pc++]; }
  // Nothing in the description below reaches memory; these are here because `MachineLike` asks for them.
  [[nodiscard]] std::uint8_t read_memory(std::uint16_t) { return 0; }
  [[nodiscard]] std::uint16_t read_memory16(std::uint16_t) { return 0; }
  void write_memory(std::uint16_t, std::uint8_t) {}
  void write_memory16(std::uint16_t, std::uint16_t) {}
  void delay(std::uint8_t) {}

  [[nodiscard]][[= refract::location]] std::uint8_t peek(const Reg which) const { return which == Reg::a ? a : x; }
  [[= refract::location]] void poke(const Reg which, const std::uint8_t value) { (which == Reg::a ? a : x) = value; }
  [[= refract::location]] void send(Port, const std::uint8_t value) { sent.push_back(value); }
};

struct Operations {
  static void nop() {}
  [[nodiscard]] static constexpr std::uint8_t ld8(const std::uint8_t value) { return value; }
  [[nodiscard]] static constexpr std::uint8_t inc8(const std::uint8_t value) {
    return static_cast<std::uint8_t>(value + 1);
  }
};

// `main` reaches its locations by name, and `on` through the register its view picked.
struct Source {
  static constexpr std::string_view file = "locations.cpu";
  static constexpr std::string_view text = R"(vocab index = a x

table main
00000001 n | lda #$nn         | ld8 a <- n
00000010   | tax              | ld8 x <- a
00000011   | out a            | ld8 out <- a
00000100   | (on a)           | goto on(a)
00000101   | (on x)           | goto on(x)
xxxxxxxx   | nop              | nop

table on(view:index)
00000001   | inc {index:view} | inc8 {index:view} <- {index:view}
00000010   | out {index:view} | ld8 out <- {index:view}
xxxxxxxx   | nop              | nop
)";
};

struct Target {
  using Machine = Renamed;
  using Compiled = refract::Compiled<Source>;
  static consteval std::vector<std::meta::info> palettes() { return {^^Operations}; }
};

// Runs `count` instructions from the start of `bytes` on a fresh machine, and returns the machine.
Renamed run(const std::initializer_list<std::uint8_t> bytes, const std::size_t count) {
  Renamed machine{.instructions_left = count};
  std::ranges::copy(bytes, machine.program.begin());
  Interpreter<Target>::run(machine);
  return machine;
}

} // namespace

TEST_CASE("A run reaches each location through the member its machine marks for it") {
  SECTION("By name") {
    // lda #5 ; tax ; out a
    const auto machine = run({0x01, 0x05, 0x02, 0x03}, 3);
    CHECK(machine.a == 5);
    CHECK(machine.x == 5);
    CHECK(machine.sent == std::vector<std::uint8_t>{5});
  }
  SECTION("Through a view") {
    // lda #5 ; tax ; on a: inc ; on x: inc ; on x: inc ; on a: out ; on x: out
    const auto machine = run({0x01, 0x05, 0x02, 0x04, 0x01, 0x05, 0x01, 0x05, 0x01, 0x04, 0x02, 0x05, 0x02}, 7);
    CHECK(machine.a == 6);
    CHECK(machine.x == 7);
    CHECK(machine.sent == std::vector<std::uint8_t>{6, 7});
  }
}

} // namespace specbolt::refract
