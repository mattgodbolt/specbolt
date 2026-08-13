// A fixed-work benchmark for the four implementations.
//
// Every implementation runs the *same* instruction stream -- zexdoc, which is
// what the regression tests already use -- for the same number of instructions,
// so the only figure that matters is nanoseconds per instruction. Fixed work
// rather than fixed time, because the point is to compare implementations
// rather than to characterise the machine they run on.
//
// The machine they run on is the problem: on a thermally limited laptop the
// same binary can differ by a third between runs. Three things help. Runs are
// interleaved, so slow drift lands on everyone rather than on whoever ran last;
// the order within a repetition alternates, so nobody is permanently last; and
// the figure reported is the *minimum* across repetitions, which is the run
// least interfered with rather than the average of the interference.
//
// None of that survives a busy machine. Read the spread column: a wide one means
// the numbers describe the laptop rather than the code.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include <lyra/lyra.hpp>

// One binary may hold every implementation, or exactly one. It matters:
// interprocedural optimisation is on for the whole link, so four
// implementations in one binary are optimised against each other's inlining
// budget and code layout, and changing one measurably changes the others. A
// per-implementation binary is the honest comparison; the combined one is what
// the emulator actually ships.
#if BENCH_V1
#include "z80/v1/Z80.hpp"
#endif
#if BENCH_V2
#include "z80/v2/Z80.hpp"
#endif
#if BENCH_V3
#include "z80/v3/Z80.hpp"
#endif
#if BENCH_V4
#include "z80/v4/Z80.hpp"
#endif

#ifdef SPECBOLT_MODULES
import peripherals;
#else
#include "peripherals/Memory.hpp"
#include "z80/common/Scheduler.hpp"
#endif

namespace specbolt {

namespace {

// The CP/M calls zexdoc makes to report progress. Answered, because the program
// stops if they are not, but not printed: this measures the CPU, and a terminal
// write in the timed region would measure the terminal.
void service_cpm(Memory &memory, RegisterFile &regs) {
  if (regs.get(RegisterFile::R8::C) == 9) {
    auto address = regs.get(RegisterFile::R16::DE);
    while (static_cast<char>(memory.read(address)) != '$')
      ++address;
  }
  const auto sp = regs.sp();
  regs.pc(memory.read16(sp));
  regs.sp(static_cast<std::uint16_t>(sp + 2));
}

template<typename Cpu>
[[nodiscard]] std::chrono::nanoseconds time_one(const std::uint64_t instructions) {
  Memory memory{4};
  memory.load(std::filesystem::path("z80/test/zexdoc.com"), 0, 0x100, 8704);
  memory.set_rom_flags({false, false, false, false});

  Scheduler scheduler;
  Cpu z80(scheduler, memory);
  z80.regs().pc(0x100);
  z80.regs().sp(0xf000);

  const auto start = std::chrono::steady_clock::now();
  for (std::uint64_t done = 0; done < instructions; ++done) {
    z80.execute_one();
    if (z80.pc() == 5) [[unlikely]]
      service_cpm(memory, z80.regs());
    else if (z80.pc() == 0) [[unlikely]]
      break;
  }
  return std::chrono::steady_clock::now() - start;
}

struct Result {
  std::string name;
  std::chrono::nanoseconds best{std::chrono::nanoseconds::max()};
  std::chrono::nanoseconds worst{};
};

struct Bench {
  std::uint64_t instructions = 20'000'000;
  std::size_t reps = 5;
  int only = 0;
  bool need_help{};

  int Main(const int argc, const char *argv[]) {
    const auto cli = lyra::cli() //
                     | lyra::help(need_help) //
                     | lyra::opt(instructions, "NUM")["-n"]["--instructions"]("Instructions to run per repetition.") //
                     | lyra::opt(reps, "NUM")["-r"]["--reps"]("Repetitions; the best of these is reported.") //
                     | lyra::opt(only, "impl")["--impl"]("Benchmark only this implementation.").choices(1, 2, 3, 4);
    if (const auto parsed = cli.parse({argc, argv}); !parsed) {
      std::print(std::cerr, "Error in command line: {}\n", parsed.message());
      return 1;
    }
    if (need_help) {
      std::cout << cli << '\n';
      return 0;
    }

    std::vector<Result> results;
    const auto wants = [&](const int impl) { return only == 0 || only == impl; };
#if BENCH_V1
    if (wants(1))
      results.push_back({.name = "v1"});
#endif
#if BENCH_V2
    if (wants(2))
      results.push_back({.name = "v2"});
#endif
#if BENCH_V3
    if (wants(3))
      results.push_back({.name = "v3"});
#endif
#if BENCH_V4
    if (wants(4))
      results.push_back({.name = "v4"});
#endif

    for (std::size_t rep = 0; rep < reps; ++rep) {
      // Alternating direction, because position within a repetition is not
      // neutral: whoever runs last meets the hottest core and the coldest
      // caches. A fixed order quietly taxes whoever is at the end of it.
      for (std::size_t at = 0; at < results.size(); ++at) {
        auto &result = results[rep % 2 == 0 ? at : results.size() - 1 - at];
        const auto taken = run_named(result.name);
        result.best = std::min(result.best, taken);
        result.worst = std::max(result.worst, taken);
      }
      std::print(std::cerr, "rep {} of {} done\n", rep + 1, reps);
    }

    report(results);
    return 0;
  }

  [[nodiscard]] std::chrono::nanoseconds run_named(const std::string &name) const {
#if BENCH_V1
    if (name == "v1")
      return time_one<v1::Z80>(instructions);
#endif
#if BENCH_V2
    if (name == "v2")
      return time_one<v2::Z80>(instructions);
#endif
#if BENCH_V3
    if (name == "v3")
      return time_one<v3::Z80>(instructions);
#endif
#if BENCH_V4
    if (name == "v4")
      return time_one<v4::Z80>(instructions);
#endif
    return {};
  }

  void report(const std::vector<Result> &results) const {
    const auto fastest = std::ranges::min(results, {}, &Result::best).best;
    std::print(std::cout, "{:>4}  {:>10}  {:>10}  {:>8}  {:>7}\n", "impl", "ns/instr", "Minstr/s", "vs best", "spread");
    for (const auto &result: results) {
      const auto per = static_cast<double>(result.best.count()) / static_cast<double>(instructions);
      // How far the worst repetition ran from the best. A wide spread means the
      // machine was busy or throttling, and the comparison is worth less.
      const auto spread = static_cast<double>(result.worst.count() - result.best.count()) /
                          static_cast<double>(result.best.count()) * 100.0;
      std::print(std::cout, "{:>4}  {:>10.2f}  {:>10.1f}  {:>7.2f}x  {:>6.1f}%\n", result.name, per, 1000.0 / per,
          static_cast<double>(result.best.count()) / static_cast<double>(fastest.count()), spread);
    }
  }
};

} // namespace

} // namespace specbolt

int main(const int argc, const char *argv[]) { return specbolt::Bench{}.Main(argc, argv); }
