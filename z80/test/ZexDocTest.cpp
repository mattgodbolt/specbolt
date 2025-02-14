#include <iostream>

#include "peripherals/Memory.hpp"
#include "z80/Disassembler.hpp"
#include "z80/Z80.hpp"

#include <catch2/catch_test_macros.hpp>

namespace specbolt {

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
      FAIL("Unsupported VT52 control character " + std::to_string(c));
    }
    output.append(1, c);
    if (c == '\n') {
      std::cout << output << std::endl;
    }
  }
};

TEST_CASE("ZEXDOC test") {
  Memory memory;
  const auto filename = std::filesystem::path("z80/test/zexdoc.com");
  static constexpr auto FileSize = 8704;
  memory.load(filename, 0x100, FileSize);
  memory.set_rom_size(0);
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

  SECTION("should pass ZEXDOC") {
    for (;;) {
      try {
        z80.execute_one();
      }
      catch (const std::runtime_error &e) {
        dump_state(std::cerr);
        FAIL(e.what());
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
            if (addr == 0x205)
              FAIL("oh no");
            for (auto c = static_cast<char>(memory.read(addr)); c != '$'; c = static_cast<char>(memory.read(++addr)))
              output.on_char(c);
          } break;
          default: FAIL("Unsupported CPM function");
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
        FAIL("PC out of bounds");
      }
      const std::uint16_t iut = 0x1d42;
      const std::uint16_t msbt = 0x0103;
      auto dump = [&](const std::uint16_t addr, const std::size_t len) {
        for (auto x = 0uz; x < len; ++x)
          std::print(std::cerr, "{:02x} ", memory.read(static_cast<std::uint16_t>(addr + x)));
      };
      if (z80.pc() == 0x1b93) {
        const auto test_addr = z80.registers().get(RegisterFile::R16::HL);
        std::print(std::cerr, "20_0 = ");
        dump(test_addr, 20);
        std::print(std::cerr, "\n20_1 = ");
        dump(test_addr + 20, 20);
        std::print(std::cerr, "\n20_2 = ");
        dump(test_addr + 40, 20);
        std::print(std::cerr, "\n");
      }
      if (z80.pc() == 0x1ba1) {
        std::print(std::cerr, "iut = ");
        dump(iut, 4);
        std::print(std::cerr, "\nmsbt = ");
        dump(msbt, 16);
        std::print(std::cerr, "\n");

        static int moose = 0;
        if (++moose == 100)
          FAIL("moose");
      }
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
    CHECK(output.output == "");
  }
}

} // namespace specbolt
