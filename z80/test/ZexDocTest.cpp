#include <iostream>

#include "peripherals/Memory.hpp"
#include "z80/Disassembler.hpp"
#include "z80/Z80.hpp"

#include <format>
#include <iostream>

namespace specbolt {

namespace {
struct Vt52Emu {
  std::string output;
  size_t ignore_next{};
  bool seen_esc{};

  void on_char(const char c) {
    if (ignore_next) {
      --ignore_next;
      return;
    }
    if (c == '\033') {
      seen_esc = true;
      return;
    }
    if (seen_esc) {
      if (c == 'Y')
        ignore_next = 2;
      return;
    }
    if (c == '\r')
      return;
    if (c < 32 && c != '\n') {
      throw std::runtime_error("Unsupported VT52 control character " + std::to_string(c));
    }
    output.append(1, c);
    std::print(std::cout, "{}", c);
    std::cout.flush();
  }
};

int Main() {
  Memory memory;
  const auto filename = std::filesystem::path("z80/test/zexdoc.com");
  static constexpr auto FileSize = 8704;
  memory.load(filename, 0x100, FileSize);
  memory.set_rom_size(0);
  // memory.write(0x120, memory.read(0x120) + 10 * 2);
  const Disassembler dis{memory};

  Z80 z80(memory);
  z80.registers().pc(0x100);
  z80.registers().sp(0xf000);
  Vt52Emu output;

  const auto dump_state = [&](auto &to) {
    for (const auto &trace: z80.history()) {
      const auto disassembled = dis.disassemble(trace.pc());
      trace.dump(to, "  ");
      std::print(to, "{}\n", disassembled.to_string());
    }
    std::print(to, "After {} instructions\n", z80.num_instructions_executed());
    std::print(to, "Output: {}\n", output.output);
  };

  // uint64_t x = 0;

  for (;;) {
    // if (++x < 100000)
    //   std::print(std::cout, "{:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x}\n", z80.pc(),
    //       z80.registers().get(RegisterFile::R16::AF), z80.registers().get(RegisterFile::R16::BC),
    //       z80.registers().get(RegisterFile::R16::DE), z80.registers().get(RegisterFile::R16::HL),
    //       z80.registers().ix(), z80.registers().iy(), z80.registers().sp());
    // else
    // break;
    try {
      z80.execute_one();
    }
    catch (const std::runtime_error &e) {
      dump_state(std::cerr);
      throw;
    }
    if (z80.pc() == 5) {
      // Emulate simple CPM console output.
      switch (z80.registers().get(RegisterFile::R8::C)) {
        case 2: // putchar()
          output.on_char(static_cast<char>(z80.registers().get(RegisterFile::R8::E)));
          break;
        case 9: {
          // print string
          auto addr = z80.registers().get(RegisterFile::R16::DE);
          for (auto c = static_cast<char>(memory.read(addr)); c != '$'; c = static_cast<char>(memory.read(++addr)))
            output.on_char(c);
        } break;
        default: throw std::runtime_error("Unsupported CPM function");
      }
      // Fake out a RET.
      z80.registers().pc(z80.pop16());
    }
    if (z80.pc() == 0) {
      dump_state(std::cout);
      break;
    }
    if (z80.pc() > FileSize + 0x100) {
      dump_state(std::cerr);
      throw std::runtime_error("PC out of bounds");
    }
    // constexpr std::uint16_t iut = 0x1d42;
    // constexpr std::uint16_t msbt = 0x0103;
    // auto dump = [&](const std::uint16_t addr, const std::size_t len) {
    //   for (auto x = 0uz; x < len; ++x)
    //     std::print(std::cerr, "{:02x} ", memory.read(static_cast<std::uint16_t>(addr + x)));
    // };
    // if (z80.pc() == 0x1b93) {
    //   const auto test_addr = z80.registers().get(RegisterFile::R16::HL);
    //   std::print(std::cerr, "20_0 = ");
    //   dump(test_addr, 20);
    //   std::print(std::cerr, "\n20_1 = ");
    //   dump(test_addr + 20, 20);
    //   std::print(std::cerr, "\n20_2 = ");
    //   dump(test_addr + 40, 20);
    //   std::print(std::cerr, "\n");
    // }
    // if (z80.pc() == 0x1ba1) {
    //   std::print(std::cerr, "iut = ");
    //   dump(iut, 4);
    //   std::print(std::cerr, "\nmsbt = ");
    //   dump(msbt, 16);
    //   std::print(std::cerr, "\n");
    //
    //   static int moose = 0;
    //   if (++moose == 100)
    //     throw std::runtime_error("moose");
    // }
    // if (z80.pc() >= 0x1cad && z80.pc() <= 0x1cda) {
    // if (z80.pc() >= 0x1b3b && z80.pc() <= 0x1b44) {
    // if (z80.pc() >= 0x1c49 && z80.pc() <= 0x1c88) {
    //   z80.registers().dump(std::cerr, "  ");
    //   const auto disassembled = dis.disassemble(z80.pc());
    //   std::print(std::cerr, "{}\n", disassembled.to_string());
    //   // static int moose = 0;
    //   // if (++moose == 5000) FAIL("moose");
    //   if (z80.pc() == 0x1c88)
    //     FAIL("baddger");
    // }
  }
  // TODO look for error/pass
  return 1;
}

} // namespace

} // namespace specbolt

int main(int, const char **) {
  try {
    return specbolt::Main();
  }
  catch (const std::runtime_error &e) {
    std::print(std::cerr, "Caught exception: {}\n", e.what());
    return 1;
  }
}
