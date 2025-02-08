#include "peripherals/Memory.hpp"
#include "peripherals/Video.hpp"
#include "z80/Disassembler.hpp"
#include "z80/Z80.hpp"

#include <format>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <sstream>

#include <readline/history.h>
#include <readline/readline.h>

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
  specbolt::Memory memory;
  specbolt::Video video{memory};
  specbolt::Z80 z80{memory};
  const specbolt::Disassembler dis{memory};
  std::unordered_set<std::uint16_t> breakpoints = {};
  std::unordered_map<std::string, std::function<int(const std::vector<std::string> &)>> commands = {};

  static App *the_app;

  App() {
    the_app = this;
    commands["help"] = [this](const std::vector<std::string> &) {
      std::cout << "Commands:\n";
      for (const auto &command: commands) {
        std::cout << "  " << command.first << "\n";
      }
      return 0;
    };
    commands["step"] = [this](const std::vector<std::string> &args) {
      step(get_number_arg(args));
      return 0;
    };
    commands["trace"] = [this](const std::vector<std::string> &args) {
      trace(get_number_arg(args));
      return 0;
    };
    commands["dump"] = [this](const std::vector<std::string> &) {
      z80.dump();
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

    rl_attempted_completion_function = [](const char *text, const int start, int) -> char ** {
      if (start != 0)
        return nullptr;
      return rl_completion_matches(text, [](const char *text, const int state) -> char * {
        if (!the_app)
          return nullptr;
        static decltype(commands)::iterator the_iterator;
        if (state == 0)
          the_iterator = std::begin(the_app->commands);
        while (the_iterator != std::end(the_app->commands)) {
          auto &command = the_iterator->first;
          ++the_iterator;
          if (command.contains(text))
            return strdup(command.c_str());
        }
        return nullptr;
      });
    };
  }
  ~App() { the_app = nullptr; }

  bool _step() {
    try {
      const auto cycles_elapsed = z80.execute_one();
      video.poll(cycles_elapsed);
    }
    catch (const std::exception &e) {
      std::print(std::cout, "Exception: {}\n", e.what());
      return true;
    }
    if (breakpoints.contains(z80.pc())) {
      std::print(std::cout, "Hit breakpoint at 0x{:04x}\n", z80.pc());
      return true;
    }
    return false;
  }

  void step(const int num_steps) {
    for (int i = 0; i < num_steps; ++i)
      if (_step())
        break;
  }

  void go() {
    while (!_step())
      ;
  }

  void trace(const int num_steps) {
    for (int i = 0; i < num_steps; ++i) {
      if (_step())
        break;
      if (i != num_steps - 1)
        report();
    }
  }

  void report() const {
    const auto disassembled = dis.disassemble(z80.pc());
    std::print(std::cout, "{}\n", disassembled.to_string());
  }

  void main() {
    memory.load("48.rom", 0, 16 * 1024);

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

      // Use ranges to split the string line on whitespace, making a vector of strings
      std::vector<std::string> inputs;
      {
        std::istringstream iss(line);
        std::copy(
            std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(), std::back_inserter(inputs));
      }
      if (const auto found = commands.find(inputs[0]); found != std::end(commands)) {
        if (const auto result =
                found->second(std::vector<std::string>(std::next(std::begin(inputs)), std::end(inputs)));
            result != 0) {
          return;
        }
      }
      else {
        std::cout << "Unknown command\n";
      }
    }
  }
};

App *App::the_app{};

} // namespace

int main() {
  try {
    App app;
    app.main();
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "Exception: " << e.what() << "\n";
    return 1;
  }
}
