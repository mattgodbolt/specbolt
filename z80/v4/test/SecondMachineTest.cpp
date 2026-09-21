#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <meta>
#include <string_view>
#include <vector>

#include "refract/Compiled.hpp"
#include "refract/Disassemble.hpp"
#include "refract/Execute.hpp"

// A second machine, described and interpreted in the same binary as the Z80.
// It exists to show that a target is a parameter: nothing about it is shared
// with the Z80 but the library.

namespace specbolt::refract {
namespace {

enum class Reg : std::uint8_t { a, x };

struct Toy {
  std::array<std::uint8_t, 64> memory{};
  std::uint16_t pc{};
  std::uint8_t a{};
  std::uint8_t x{};
  std::size_t cycles{};
  std::size_t instructions_left{};

  bool start_instruction() {
    if (instructions_left == 0)
      return false;
    --instructions_left;
    return true;
  }
  [[nodiscard]] std::uint8_t fetch_opcode() {
    cycles += 2;
    return memory[pc++ % memory.size()];
  }
  [[nodiscard]] std::uint8_t fetch_immediate() {
    ++cycles;
    return memory[pc++ % memory.size()];
  }
  [[nodiscard]] std::uint16_t fetch_immediate16() {
    const auto low = fetch_immediate();
    return static_cast<std::uint16_t>(fetch_immediate() << 8 | low);
  }
  [[nodiscard]] std::uint8_t read_memory(const std::uint16_t address) {
    ++cycles;
    return memory[address % memory.size()];
  }
  [[nodiscard]] std::uint16_t read_memory16(const std::uint16_t address) {
    const auto low = read_memory(address);
    return static_cast<std::uint16_t>(read_memory(static_cast<std::uint16_t>(address + 1)) << 8 | low);
  }
  void write_memory(const std::uint16_t address, const std::uint8_t value) {
    ++cycles;
    memory[address % memory.size()] = value;
  }
  void write_memory16(const std::uint16_t address, const std::uint16_t value) {
    write_memory(address, static_cast<std::uint8_t>(value));
    write_memory(static_cast<std::uint16_t>(address + 1), static_cast<std::uint8_t>(value >> 8));
  }
  [[nodiscard]] std::uint16_t displaced_address(
      const std::uint16_t base, const std::uint8_t offset, const std::uint8_t /*immediate_bytes*/) {
    return static_cast<std::uint16_t>(base + offset);
  }
  void delay(const std::uint8_t count) { cycles += count; }

  [[nodiscard]] std::uint8_t read(const Reg which) const { return which == Reg::a ? a : x; }
  void write(const Reg which, const std::uint8_t value) { (which == Reg::a ? a : x) = value; }

  // Two verbs the machine marks, one that needs it and one that does not.
  // `delay` above is public and is not a verb, because nothing says it is.
  [[nodiscard]][[= refract::operation]] std::uint8_t swap(const std::uint8_t value) {
    ++cycles;
    return static_cast<std::uint8_t>(value << 4 | value >> 4);
  }
  [[nodiscard]][[= refract::operation]] static std::uint8_t twice(const std::uint8_t value) {
    return static_cast<std::uint8_t>(value * 2);
  }
};

struct ToyOperations {
  static void nop() {}
  [[nodiscard]] static std::uint8_t ld8(const std::uint8_t value) { return value; }
  [[nodiscard]] static std::uint8_t add8(const std::uint8_t lhs, const std::uint8_t rhs) {
    return static_cast<std::uint8_t>(lhs + rhs);
  }
};

inline constexpr std::string_view toy_cpu = R"(vocab reg : Reg = a x

table main
00000000   | nop            | nop
0000001r n | ld {reg:r}, $nn | ld8 {reg:r} <- n
0000010r   | add a, {reg:r} | add8 a <- a {reg:r}
00000110   | swap a         | swap a <- a
00000111   | twice a        | twice a <- a
xxxxxxxx   | ??             | nop
)";

struct ToyTarget {
  using Machine = Toy;
  using Compiled = refract::Compiled<toy_cpu, "toy.cpu">;
  static consteval std::vector<std::meta::info> palettes() { return {^^ToyOperations}; }
};

} // namespace

TEST_CASE("A second machine runs beside the Z80") {
  // ld a, 5 ; ld x, 7 ; add a, x ; swap a ; twice a
  Toy toy{.memory = {0x02, 0x05, 0x03, 0x07, 0x05, 0x06, 0x07}, .instructions_left = 5};
  refract::Interpreter<ToyTarget>::run(toy);
  CHECK(toy.a == 0x80); // 12 is 0x0c, swapped is 0xc0, doubled is 0x80
  CHECK(toy.x == 7);
  CHECK(toy.pc == 7);
  CHECK(toy.cycles == 2 + 1 + 2 + 1 + 2 + (2 + 1) + 2);

  const auto [text, length] = refract::disassemble(
      ToyTarget::Compiled::description(), 2, [&](const std::size_t offset) { return toy.memory[2 + offset]; });
  CHECK(text == "ld x, 0x07");
  CHECK(length == 2);
}

} // namespace specbolt::refract
