#include <format>
#include <iostream>
#include <stdexcept>

#include "peripherals/Memory.hpp"
#include "peripherals/Video.hpp"
#include "z80/Disassembler.hpp"
#include "z80/Z80.hpp"

namespace {

void Main() {
  specbolt::Memory memory;
  specbolt::Video video(memory);
  memory.load("48.rom", 0, 16 * 1024);
  specbolt::Z80 z80(memory);
  // static constexpr auto cycles_per_frame = static_cast<std::size_t>(3.5 * 1'000'000 / 50);

  const specbolt::Disassembler dis(memory);
  try {
    int trace{};
    for (int i = 0; i < 220'000; ++i) {
      const auto disassembled = dis.disassemble(z80.pc());
      if (trace)
        std::cout << disassembled.to_string() << "\n";
      const auto cycles_elapsed = z80.execute_one();
      video.poll(cycles_elapsed);
      // TODO update this to trace a specific address or whatever
      if (!trace && z80.regs().get(specbolt::RegisterFile::R16::HL) == 0x4000) {
        trace = 9999;
      }
      if (trace) {
        z80.dump();
        if (--trace == 0) {
          break;
        }
      }
    }
  }
  catch (const std::runtime_error &runtime_error) {
    std::print(std::cout, "Error: {}\n", runtime_error.what());
    z80.dump();
    return;
  }
  z80.dump();
}

} // namespace

int main() {
  try {
    Main();
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "Exception: " << e.what() << "\n";
    return 1;
  }
}
