#include <iostream>

#include <filesystem>
#include <format>
#include <lyra/lyra.hpp>

import z80;
import peripherals;


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

int Main(int argc, const char *argv[]) {
  uint64_t dump_instructions = 0;
  int skip = 0;
  bool need_help{};
  bool new_impl{};
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

  Memory memory;
  const auto filename = std::filesystem::path("z80/test/zexdoc.com");
  static constexpr auto FileSize = 8704;
  memory.load(filename, 0x100, FileSize);
  memory.set_rom_size(0);
  if (skip >= 0)
    memory.write16(0x120, static_cast<std::uint16_t>(memory.read16(0x120) + skip * 2));
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

  uint64_t instructions_executed = 0;

  while (z80.pc() != 0) {
    if (dump_instructions) {
      if (++instructions_executed < dump_instructions)
        std::print(std::cout, "{:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:02x}{:02x}{:02x}{:02x}\n",
            z80.pc(), z80.registers().get(RegisterFile::R16::AF), z80.registers().get(RegisterFile::R16::BC),
            z80.registers().get(RegisterFile::R16::DE), z80.registers().get(RegisterFile::R16::HL),
            z80.registers().ix(), z80.registers().iy(), z80.registers().sp(), memory.read(0x1d42), memory.read(0x1d43),
            memory.read(0x1d44), memory.read(0x1d45));
      else
        break;
    }
    try {
      if (new_impl)
        decode_and_run(z80);
      else
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
      z80.registers().pc(z80.pop16());
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

} // namespace

} // namespace specbolt

int main(const int argc, const char *argv[]) {
  try {
    return specbolt::Main(argc, argv);
  }
  catch (const std::runtime_error &e) {
    std::print(std::cerr, "Caught exception: {}\n", e.what());
    return 1;
  }
}
