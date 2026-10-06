#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <exception>
#include <meta>
#include <string_view>
#include <vector>

#include "refract/Compiled.hpp"
#include "refract/Execute.hpp"

// What `Interpreter` reports when a description names something the machine does not supply, or a machine is marked in
// a way the generator cannot use. Its lookups are `consteval`, so `CHECK_THROWS_WITH` cannot call them; constant
// evaluation can catch an exception, though, so each message is pinned exactly here and a regression fails the build.
//
// Each is thrown by a `consteval` function the test calls directly. Instantiating a handler that fails the same check
// would be a hard error, with nothing to catch it.

namespace specbolt::refract {
namespace {

#if __cpp_constexpr_exceptions

// Whether running `probe` during constant evaluation throws, with exactly `expected` as its message.
consteval bool throws_with(auto probe, const std::string_view expected) {
  try {
    probe();
  }
  catch (const std::exception &error) {
    return std::string_view(error.what()) == expected;
  }
  return false;
}

enum class Reg : std::uint8_t { a, x };
enum class Other : std::uint8_t { a, y };
enum class Way : std::uint8_t {
  Up[[= refract::Spelling{"i"}]],
  Down[[= refract::Spelling{"d"}]],
};

// Everything `MachineLike` asks for, and nothing a description can name. Each machine below adds what its test needs;
// none of them is ever run.
struct Bare {
  bool start_instruction() { return false; }
  std::uint8_t fetch_opcode() { return 0; }
  std::uint8_t fetch_immediate() { return 0; }
  std::uint8_t read_memory(std::uint16_t) { return 0; }
  std::uint16_t read_memory16(std::uint16_t) { return 0; }
  void write_memory(std::uint16_t, std::uint8_t) {}
  void write_memory16(std::uint16_t, std::uint16_t) {}
  void delay(std::uint8_t) {}
};

// A machine whose one kind of location can be read and not written.
struct Plain : Bare {
  [[nodiscard]][[= refract::location]] std::uint8_t read(Reg) const { return 0; }
};

struct Shadowing : Bare {
  [[nodiscard]][[= refract::location]] std::uint8_t read(Reg) const { return 0; }
  [[nodiscard]][[= refract::location]] std::uint8_t read(Other) const { return 0; }
};

// Accessors named as the machine pleases, and a location that can be written and not read.
struct Renamed : Bare {
  [[nodiscard]][[= refract::location]] std::uint8_t peek(Reg) const { return 0; }
  [[= refract::location]] void poke(Reg, std::uint8_t) {}
  [[= refract::location]] void latch(Other, std::uint8_t) {}
};

// An accessor that is an operator, and so has no identifier.
struct Indexed : Bare {
  [[nodiscard]][[= refract::location]] std::uint8_t operator[](Reg) const { return 0; }
};

// A machine whose description may have displaced rows, with a window that holds no bytes.
struct Displacing : Plain {
  static constexpr std::uint8_t displacement_window_bytes = 0;
  std::uint16_t displaced_address(const std::uint16_t base, std::int8_t, std::uint8_t) { return base; }
};

// Machines whose locations are marked wrongly, one way each.
struct MisshapenRead : Bare {
  [[nodiscard]][[= refract::location]] std::uint8_t read(Reg, int) const { return 0; }
};
struct MisshapenWrite : Bare {
  [[= refract::location]] void write(Reg) {}
};
struct ConstructorRead : Bare {
  [[= refract::location]] explicit ConstructorRead(Reg) {}
};
struct ConstructorWrite : Bare {
  [[= refract::location]] ConstructorWrite(Reg, std::uint8_t) {}
};
struct PrivateRead : Bare {
private:
  [[nodiscard]][[= refract::location]] std::uint8_t read(Reg) const { return 0; }
};
struct TwoReads : Bare {
  [[nodiscard]][[= refract::location]] std::uint8_t read(Reg) const { return 0; }
  [[nodiscard]][[= refract::location]] std::uint8_t peek(Reg) const { return 0; }
};
struct Disagreeing : Bare {
  [[nodiscard]][[= refract::location]] std::uint8_t read(Reg) const { return 0; }
  [[= refract::location]] void write(Reg, std::uint16_t) {}
};
struct TwoReadsOneIndexed : Bare {
  [[nodiscard]][[= refract::location]] std::uint8_t operator[](Reg) const { return 0; }
  [[nodiscard]][[= refract::location]] std::uint8_t read(Reg) const { return 0; }
};
struct DisagreeingIndexed : Bare {
  [[nodiscard]][[= refract::location]] std::uint8_t operator[](Reg) const { return 0; }
  [[= refract::location]] void write(Reg, std::uint16_t) {}
};

struct Secretive : Bare {
  [[nodiscard]][[= refract::location]] std::uint8_t read(Reg) const { return 0; }

private:
  [[nodiscard]][[= refract::operation]] static std::uint8_t secret(const std::uint8_t value) { return value; }
};

struct Operations {
  static void nop() {}
  [[nodiscard]] static std::uint8_t twice(const std::uint8_t value) { return static_cast<std::uint8_t>(value * 2); }
  [[nodiscard]] static Continue zero(const std::uint8_t value) { return continue_if(value == 0); }
};

struct MoreOperations {
  [[nodiscard]] static std::uint8_t twice(const std::uint8_t value) { return value; }
};

struct PlainSource {
  static constexpr std::string_view file = "plain.cpu";
  static constexpr std::string_view text = R"(vocab reg : Reg = a x

table main
xxxxxxxx   | nop            | nop
)";
};

struct MixedSource {
  static constexpr std::string_view file = "mixed.cpu";
  static constexpr std::string_view text = R"(vocab test = z:zero n:twice

table main
xxxxxxxx   | nop            | nop
)";
};

template<typename Source, typename M, typename... Palettes>
struct Probe {
  using Machine = M;
  using Compiled = refract::Compiled<Source>;
  static consteval std::vector<std::meta::info> palettes() { return {^^Palettes...}; }
};

template<typename M, typename... Palettes>
using Generator = Interpreter<Probe<PlainSource, M, Palettes...>>;

struct Pair {
  std::uint8_t low;
  std::uint8_t high;
};
struct Extended : Pair {
  std::uint8_t top;
};
struct Whole {
  constexpr explicit Whole(const std::uint8_t value) : value_(value) {}

private:
  std::uint8_t value_;
};

#endif

} // namespace

#if __cpp_constexpr_exceptions

TEST_CASE("A name the machine does not supply is reported against its line") {
  using Lookups = Generator<Plain, Operations>;
  STATIC_CHECK(
      throws_with([] { return Lookups::find_operation("ld17", 3); }, "plain.cpu:3: this CPU has nothing named 'ld17'"));
  STATIC_CHECK(
      throws_with([] { return Lookups::find_location("q", 5); }, "plain.cpu:5: this CPU has nothing named 'q'"));
  // A vocabulary that names its scope searches nowhere else, so a location in another scope is not found.
  STATIC_CHECK(Generator<Shadowing, Operations>::find_location("y", 1) == ^^Other::y);
  STATIC_CHECK(throws_with([] { return Generator<Shadowing, Operations>::find_location("y", 5, "Reg"); },
      "plain.cpu:5: this CPU has nothing named 'y'"));
  STATIC_CHECK(throws_with(
      [] { return Lookups::find_scope("Nope", 2); }, "plain.cpu:2: no scope named 'Nope' (this CPU offers Reg)"));
  STATIC_CHECK(throws_with([] { return Lookups::find_spelling(^^Way, "x", 6); },
      "plain.cpu:6: no member of 'Way' is called 'x' (it has i, d)"));
  // And what is there is found, ignoring case.
  STATIC_CHECK(Lookups::find_location("A", 1) == ^^Reg::a);
  STATIC_CHECK(Lookups::find_spelling(^^Way, "D", 1) == ^^Way::Down);
}

TEST_CASE("A name that means two things, or one the description cannot reach, is reported") {
  STATIC_CHECK(throws_with([] { return Generator<Plain, Operations, MoreOperations>::find_operation("twice", 3); },
      "plain.cpu:3: this CPU has more than one thing named 'twice' (in Operations, MoreOperations)"));
  STATIC_CHECK(throws_with([] { return Generator<Secretive, Operations>::find_operation("secret", 4); },
      "plain.cpu:4: 'secret' is marked as an operation but is not public, so a description cannot reach it"));
}

TEST_CASE("A location is reached through the members the machine marks, whatever they are called") {
  using Lookups = Generator<Renamed, Operations>;
  STATIC_CHECK(Lookups::accessor_for(^^Reg, Lookups::Access::read, "a", 1) == ^^Renamed::peek);
  STATIC_CHECK(Lookups::accessor_for(^^Reg, Lookups::Access::write, "a", 1) == ^^Renamed::poke);
  STATIC_CHECK(Lookups::accessor_for(^^Other, Lookups::Access::write, "y", 1) == ^^Renamed::latch);
  using Operator = Generator<Indexed, Operations>;
  STATIC_CHECK(Operator::accessor_for(^^Reg, Operator::Access::read, "a", 1) == ^^Indexed::operator[]);
  // A location with only one of the two is a location all the same, and its names are found.
  STATIC_CHECK(Lookups::find_location("y", 1) == ^^Other::y);
}

TEST_CASE("A row reaching a location in a way the machine does not offer is reported against its line") {
  using ReadOnly = Generator<Plain, Operations>;
  using Named = Generator<Renamed, Operations>;
  STATIC_CHECK(throws_with([] { return ReadOnly::accessor_for(^^Reg, ReadOnly::Access::write, "a", 4); },
      "plain.cpu:4: this row writes 'a', and nothing marked [[=refract::location]] writes Reg"));
  STATIC_CHECK(throws_with([] { return Named::accessor_for(^^Other, Named::Access::read, "y", 6); },
      "plain.cpu:6: this row reads 'y', and nothing marked [[=refract::location]] reads Other"));
}

TEST_CASE("A machine whose locations are marked wrongly is reported") {
  STATIC_CHECK(throws_with([] { Generator<Shadowing, Operations>::check_location_names_unique(); },
      "two of this machine's locations are spelled 'a' (in Reg and Other), so a description could not say which it "
      "meant"));
  // Neither shape: two parameters and a result, one parameter and no result, or not callable on the machine at all.
  constexpr auto neither = " is marked [[=refract::location]], so it must be a public member function taking an enum, "
                           "the location it reaches, and either returning what the location holds, to read it, or "
                           "taking the value to store and returning nothing, to write it";
  STATIC_CHECK(throws_with(
      [] { return Generator<MisshapenRead, Operations>::scan_locations(); }, std::string("'read'") + neither));
  STATIC_CHECK(throws_with(
      [] { return Generator<MisshapenWrite, Operations>::scan_locations(); }, std::string("'write'") + neither));
  STATIC_CHECK(throws_with(
      [] { return Generator<PrivateRead, Operations>::scan_locations(); }, std::string("'read'") + neither));
  // A constructor has the shape of an accessor, and is not one: it cannot be called on a machine.
  STATIC_CHECK(throws_with(
      [] { return Generator<ConstructorRead, Operations>::scan_locations(); }, std::string("a constructor") + neither));
  STATIC_CHECK(throws_with([] { return Generator<ConstructorWrite, Operations>::scan_locations(); },
      std::string("a constructor") + neither));
  STATIC_CHECK(throws_with([] { return Generator<TwoReads, Operations>::scan_locations(); },
      "'read' and 'peek' both read Reg, so there is no saying which to call"));
  STATIC_CHECK(throws_with([] { return Generator<Disagreeing, Operations>::scan_locations(); },
      "Reg is read as unsigned char by 'read' and written as short unsigned int by 'write'; a location holds one "
      "type"));
  // An operator has no identifier, so it is named by its symbol.
  STATIC_CHECK(throws_with([] { return Generator<TwoReadsOneIndexed, Operations>::scan_locations(); },
      "'operator[]' and 'read' both read Reg, so there is no saying which to call"));
  STATIC_CHECK(throws_with([] { return Generator<DisagreeingIndexed, Operations>::scan_locations(); },
      "Reg is read as unsigned char by 'operator[]' and written as short unsigned int by 'write'; a location holds "
      "one type"));
}

TEST_CASE("A displaced row is checked against what its machine can do") {
  STATIC_CHECK(throws_with([] { Generator<Plain, Operations>::check_machine_displaces(0, 7); },
      "plain.cpu:7: this row is displaced, so the machine needs displaced_address and displacement_window_bytes "
      "(DisplacingMachine in Machine.hpp)"));
  STATIC_CHECK(throws_with([] { Generator<Displacing, Operations>::check_machine_displaces(1, 7); },
      "plain.cpu:7: this row reads 1 byte(s) after its displacement and before its address is formed, which is more "
      "than this machine's displacement_window_bytes allows"));
  STATIC_CHECK(!throws_with([] { Generator<Displacing, Operations>::check_machine_displaces(0, 7); }, ""));
}

TEST_CASE("A vocabulary that mixes conditions with operations is reported") {
  STATIC_CHECK(throws_with([] { Interpreter<Probe<MixedSource, Plain, Operations>>::check_conditions_agree(); },
      "mixed.cpu:1: vocabulary 'test' mixes conditions with operations that are not ('zero' returns Continue and "
      "'twice' does not), so a row naming it would branch at some opcodes and run straight on at others"));
}

TEST_CASE("A result splits only when it is an aggregate without a base class") {
  using Lookups = Generator<Plain, Operations>;
  STATIC_CHECK(Lookups::decomposes_into(^^Pair, 1).size() == 2);
  STATIC_CHECK(Lookups::decomposes_into(^^Whole, 1).empty());
  STATIC_CHECK(throws_with([] { return Lookups::decomposes_into(^^Extended, 4); },
      "plain.cpu:4: this operation returns a type with a base class, whose members a row could not be given"));
}

#else

TEST_CASE("The interpreter's diagnostics") {
  SKIP("this compiler cannot catch an exception during constant evaluation, which is how these are tested");
}

#endif

} // namespace specbolt::refract
