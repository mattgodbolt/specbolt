// Microbenchmark: time-per-instruction across v1/v2/v3/v4 on the same
// tight loop. Only uses opcodes v4 fully supports (base + CB), so we get
// an apples-to-apples comparison across all four implementations.

#include "peripherals/Memory.hpp"
#include "z80/common/Scheduler.hpp"
#include "z80/v1/Z80.hpp"
#include "z80/v2/Z80.hpp"
#include "z80/v3/Z80.hpp"
#ifdef SPECBOLT_V4
#include "z80/v4/Z80.hpp"
#endif

#include <chrono>
#include <cstdint>
#include <print>
#include <string>

namespace specbolt {

// Tight loop: at $8000:
//   ADD A, B         ; 0x80   (ALU)
//   INC C            ; 0x0c   (8-bit inc)
//   ADD HL, BC       ; 0x09   (16-bit add)
//   JR  $8000        ; 0x18 fa  (backwards relative jump)
constexpr std::array<std::uint8_t, 5> program = {
    0x80, 0x0c, 0x09, 0x18, 0xfa};

template <typename Cpu>
double run(std::uint64_t steps, std::string_view label) {
  Memory memory{4};
  for (std::size_t i = 0; i < program.size(); ++i)
    memory.write(static_cast<std::uint16_t>(0x8000 + i), program[i]);

  Scheduler scheduler;
  Cpu cpu(scheduler, memory);
  cpu.regs().pc(0x8000);
  cpu.regs().sp(0xf000);

  const auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < steps; ++i) cpu.execute_one();
  const auto t1 = std::chrono::steady_clock::now();
  const auto secs = std::chrono::duration<double>(t1 - t0).count();
  const auto ns_per = secs * 1e9 / static_cast<double>(steps);
  std::println("{:>6}  {:>10} steps  {:>8.3f} s  {:>7.2f} ns/insn",
               label, steps, secs, ns_per);
  return ns_per;
}

} // namespace specbolt

int main(int argc, char *argv[]) {
  std::uint64_t steps = 100'000'000;
  if (argc > 1) steps = std::stoull(argv[1]);

  specbolt::run<specbolt::v1::Z80>(steps, "v1");
  specbolt::run<specbolt::v2::Z80>(steps, "v2");
  specbolt::run<specbolt::v3::Z80>(steps, "v3");
#ifdef SPECBOLT_V4
  specbolt::run<specbolt::v4::Z80>(steps, "v4");
#endif
  return 0;
}
