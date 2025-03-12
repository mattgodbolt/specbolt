#include <csignal>
#include <format>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

#include <unordered_map>
#include <unordered_set>

#include <lyra/lyra.hpp>
#include <readline/history.h>
#include <readline/readline.h>

#ifdef SPECBOLT_MODULES
import peripherals;
import spectrum;
import z80_v1;
#else
#include "peripherals/Memory.hpp"
#include "peripherals/Video.hpp"
#include "spectrum/Spectrum.hpp"
#include "z80/v1/Disassembler.hpp"
#include "z80/v1/Z80.hpp"
#endif

namespace {

int parse_num(const std::string &number) {
  if (number.starts_with("0x"))
    return std::stoi(number, nullptr, 16);
  return std::stoi(number);
}

int get_number_arg(const std::vector<std::string> &args) {
  if (args.empty())
    return 1;
  return parse_num(args[0]);
}

struct App {
  specbolt::Spectrum<specbolt::v1::Z80> spectrum{"48.rom", 48'000}; // TODO make configurable
  const specbolt::v1::Disassembler dis{spectrum.memory()};
  std::unordered_set<std::uint16_t> breakpoints = {};
  std::unordered_map<std::string, std::function<int(const std::vector<std::string> &)>> commands = {};
  std::atomic<bool> interrupted{false};

  static App *&self() {
    static App *the_app = nullptr;
    return the_app;
  }

  App() {
    self() = this;
    commands["help"] = [this](const std::vector<std::string> &) {
      std::cout << "Commands:\n";
      for (const auto &command: commands) {
        std::cout << "  " << command.first << "\n";
      }
      return 0;
    };
    commands["quit"] = [](const std::vector<std::string> &) { return 1; };
    commands["step"] = [this](const std::vector<std::string> &args) {
      step(get_number_arg(args));
      return 0;
    };
    commands["history"] = [this](const std::vector<std::string> &) {
      history();
      return 0;
    };
    commands["next"] = [this](const std::vector<std::string> &args) {
      next(get_number_arg(args));
      return 0;
    };
    commands["trace"] = [this](const std::vector<std::string> &args) {
      trace(get_number_arg(args));
      return 0;
    };
    commands["dump"] = [this](const std::vector<std::string> &args) {
      if (args.empty())
        spectrum.z80().dump();
      else {
        const auto address = parse_num(args[0]);
        const auto &memory = spectrum.memory();
        std::print(std::cout, "0x{:04x}: 0x{:02x} 0x{:02x} 0x{:02x} 0x{:02x} 0x{:02x} 0x{:02x} 0x{:02x} 0x{:02x}\n",
            address, memory.read(static_cast<std::uint16_t>(address + 0)),
            memory.read(static_cast<std::uint16_t>(address + 1)), memory.read(static_cast<std::uint16_t>(address + 2)),
            memory.read(static_cast<std::uint16_t>(address + 3)), memory.read(static_cast<std::uint16_t>(address + 4)),
            memory.read(static_cast<std::uint16_t>(address + 4)), memory.read(static_cast<std::uint16_t>(address + 6)),
            memory.read(static_cast<std::uint16_t>(address + 7)));
      }
      return 0;
    };
    commands["break"] = [this](const std::vector<std::string> &args) {
      if (args.empty()) {
        std::print(std::cout, "Breakpoints:\n");
        for (auto b: breakpoints)
          std::print(std::cout, "  0x{:04x}\n", b);
      }
      for (const auto &arg: args)
        breakpoints.insert(static_cast<std::uint16_t>(parse_num(arg)));
      return 0;
    };
    commands["go"] = [this](const std::vector<std::string> &) {
      go();
      return 0;
    };
    commands["reset"] = [this](const std::vector<std::string> &) {
      spectrum.z80().registers().pc(0);
      return 0;
    };

    rl_attempted_completion_function = [](const char *text, const int start, int) -> char ** {
      if (start != 0)
        return nullptr;
      return rl_completion_matches(text, [](const char *text, const int state) -> char * {
        if (!self())
          return nullptr;
        static decltype(commands)::iterator the_iterator;
        if (state == 0)
          the_iterator = std::begin(self()->commands);
        while (the_iterator != std::end(self()->commands)) {
          auto &command = the_iterator->first;
          ++the_iterator;
          if (command.contains(text))
            return strdup(command.c_str());
        }
        return nullptr;
      });
    };
  }
  ~App() { self() = nullptr; }

  bool interrupt() { return interrupted.exchange(true); }

  bool _check_for_interrupt_or_breakpoint() {
    if (interrupted.exchange(false)) {
      std::print(std::cout, "Interrupted\n");
      return true;
    }
    if (breakpoints.contains(spectrum.z80().pc())) {
      std::print(std::cout, "Hit breakpoint at 0x{:04x}\n", spectrum.z80().pc());
      return true;
    }
    return false;
  }

  bool _step() {
    try {
      spectrum.run_cycles(1);
    }
    catch (const std::exception &e) {
      std::print(std::cout, "Exception: {}\n", e.what());
      return true;
    }
    return _check_for_interrupt_or_breakpoint();
  }

  bool _next() {
    try {
      const auto prev_pc = spectrum.z80().pc();
      do {
        spectrum.run_cycles(1);
      }
      while (spectrum.z80().pc() == prev_pc);
    }
    catch (const std::exception &e) {
      std::print(std::cout, "Exception: {}\n", e.what());
      return true;
    }
    return _check_for_interrupt_or_breakpoint();
  }

  void step(const int num_steps) {
    for (int i = 0; i < num_steps; ++i)
      if (_step())
        break;
  }

  void next(const int num_instructions) {
    for (int i = 0; i < num_instructions; ++i)
      if (_next())
        break;
  }

  void go() {
    while (!_step())
      ;
  }

  void trace(const int num_steps) {
    for (int i = 0; i < num_steps; ++i) {
      if (_next())
        break;
      if (i != num_steps - 1)
        report();
    }
  }

  void history() const {
    for (const auto &trace: spectrum.z80().history()) {
      const auto disassembled = dis.disassemble(trace.pc());
      trace.dump(std::cout, "  ");
      std::print(std::cout, "{}\n", disassembled.to_string());
    }
  }

  void report() const {
    const auto disassembled = dis.disassemble(spectrum.z80().pc());
    std::print(std::cout, "{} ({} executed)\n", disassembled.to_string(), spectrum.z80().num_instructions_executed());
  }

  std::vector<std::string> exec_on_startup;
  int main(int argc, const char **argv) {
    bool need_help{};
    const auto cli = lyra::cli() //
                     | lyra::help(need_help) //
                     | lyra::opt(exec_on_startup, "cmd")["-x"]["--execute-on-startup"]("Execute command on startup");
    if (const auto parse_result = cli.parse({argc, argv}); !parse_result) {
      std::print(std::cerr, "Error in command line: {}\n", parse_result.message());
      return 1;
    }
    if (need_help) {
      std::cout << cli << '\n';
      return 0;
    }

    for (const auto &cmd: exec_on_startup)
      execute(cmd);

    std::string line;
    while (true) {
      report();
      char *raw_line{readline("> ")};
      if (!raw_line)
        break;
      if (raw_line[0]) {
        line = raw_line;
      }
      free(raw_line);

      if (line.empty())
        continue;
      add_history(line.c_str());

      if (!execute(line))
        break;
    }
    return 0;
  }

  bool execute(const std::string &line) {
    // Use ranges to split the string line on whitespace, making a vector of strings
    std::vector<std::string> inputs;
    {
      std::istringstream iss(line);
      std::copy(
          std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(), std::back_inserter(inputs));
    }
    if (const auto found = commands.find(inputs[0]); found != std::end(commands)) {
      if (const auto result = found->second(std::vector(std::next(std::begin(inputs)), std::end(inputs)));
          result != 0) {
        return false;
      }
    }
    else {
      std::cout << "Unknown command " << inputs[0] << "\n";
    }
    return true;
  }
};

} // namespace

int main(const int argc, const char **argv) {
  App app;

  sigset_t blocked_signals{};
  // todo check errors etc. extract?
  sigemptyset(&blocked_signals);
  sigaddset(&blocked_signals, SIGINT);
  sigprocmask(SIG_BLOCK, &blocked_signals, nullptr);

  std::jthread t([&](const std::stop_token &stop_token) {
    while (!stop_token.stop_requested()) {
      int signum{};
      if (sigwait(&blocked_signals, &signum) != -1) {
        app.interrupt();
        continue;
      }
      if (errno != EAGAIN && errno != EINTR) {
        throw std::system_error(errno, std::generic_category());
      }
    }
  });
  struct SelfInterrupt {
    ~SelfInterrupt() { kill(0, SIGINT); }
  } self_irq;

  try {
    return app.main(argc, argv);
  }
  catch (const std::exception &e) {
    std::cerr << "Exception: " << e.what() << "\n";
    return 1;
  }
}
