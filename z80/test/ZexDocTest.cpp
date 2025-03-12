#include <iostream>

#include "../v2/include/z80/v2/Z80.hpp"
#include "peripherals/Memory.hpp"
#include "z80/v1/Disassembler.hpp"
#include "z80/v1/Z80.hpp"

#include <format>
#include <lyra/lyra.hpp>

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

struct ZexDocTest {
  uint64_t dump_instructions = 0;
  int skip = 0;
  bool need_help{};
  bool new_impl{};

  template<typename DUT>
  int run_test() {
    Memory memory;
    const auto filename = std::filesystem::path("z80/test/zexdoc.com");
    static constexpr auto FileSize = 8704;
    memory.load(filename, 0x100, FileSize);
    memory.set_rom_size(0);
    if (skip >= 0)
      memory.write16(0x120, static_cast<std::uint16_t>(memory.read16(0x120) + skip * 2));
    const v1::Disassembler dis{memory};

    DUT z80(memory);
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

    uint64_t instructions_executed = 0;

    while (z80.pc() != 0) {
      if (dump_instructions) {
        if (++instructions_executed < dump_instructions)
          std::print(std::cout, "{:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:02x}{:02x}{:02x}{:02x}\n",
              z80.pc(), z80.registers().get(RegisterFile::R16::AF), z80.registers().get(RegisterFile::R16::BC),
              z80.registers().get(RegisterFile::R16::DE), z80.registers().get(RegisterFile::R16::HL),
              z80.registers().ix(), z80.registers().iy(), z80.registers().sp(), memory.read(0x1d42),
              memory.read(0x1d43), memory.read(0x1d44), memory.read(0x1d45));
        else
          break;
      }
      try {
        z80.execute_one();
      }
      catch (const std::runtime_error &) {
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
        const auto sp = z80.registers().sp();
        z80.registers().pc(memory.read16(sp));
        z80.registers().sp(sp + 2);
      }
    }
    std::print(std::cout, "\n---\n");
    if (!output.output.contains("Tests complete")) {
      std::print(std::cerr, "Output missing 'Test complete'.\n");
      return 1;
    }
    if (output.output.contains("ERROR")) {
      std::print(std::cerr, "Found errors.\n");
      return 1;
    }
    std::print(std::cout, "All tests passed!\n");
    return 0;
  }

  int Main(int argc, const char *argv[]) {
    const auto cli = lyra::cli() //
                     | lyra::help(need_help) //
                     | lyra::opt(dump_instructions, "NUM")["-d"]["--dump-instructions"](
                           "Dump the first NUM instructions, then exit.") //
                     | lyra::opt(new_impl)["--new-impl"]("Use new implementation.") //
                     | lyra::opt(skip, "NUM")["-s"]["--skip"]("Skip the first NUM tests.");
    if (const auto parse_result = cli.parse({argc, argv}); !parse_result) {
      std::print(std::cerr, "Error in command line: {}\n", parse_result.message());
      return 1;
    }
    if (need_help) {
      std::cout << cli << '\n';
      return 0;
    }
    if (new_impl)
      return run_test<v2::Z80>();
    return run_test<v1::Z80>();
  }
};

} // namespace

} // namespace specbolt

int main(const int argc, const char *argv[]) {
  try {
    specbolt::ZexDocTest test;
    return test.Main(argc, argv);
  }
  catch (const std::runtime_error &e) {
    std::print(std::cerr, "Caught exception: {}\n", e.what());
    return 1;
  }
}
