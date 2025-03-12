#include "sdl_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

#include <lyra/lyra.hpp>

#ifdef SPECBOLT_MODULES
import peripherals;
import spectrum;
import z80_v1;
import z80v2;
#else
#include "peripherals/Video.hpp"
#include "spectrum/Snapshot.hpp"
#include "spectrum/Spectrum.hpp"
#include "z80/v1/Disassembler.hpp"
#include "z80/v1/Z80.hpp"
#include "z80/v2/Z80.hpp"
#endif


namespace specbolt {

namespace {

struct SdlApp {
  std::filesystem::path rom{"48.rom"};
  std::filesystem::path snapshot;
  bool need_help{};
  std::size_t trace_instructions{};
  bool new_impl{};

  int Main(const int argc, const char *argv[]) {
    const auto cli = lyra::cli() //
                     | lyra::help(need_help) //
                     | lyra::opt(rom, "ROM")["--rom"]("Where to find the ROM") //
                     | lyra::opt(trace_instructions, "NUM")["--trace"]("Trace the first NUM instructions") //
                     | lyra::opt(new_impl)["--new-impl"]("Use new implementation.") //
                     | lyra::arg(snapshot, "SNAPSHOT")("Snapshot to load");
    if (const auto parse_result = cli.parse({argc, argv}); !parse_result) {
      std::println(std::cerr, "Error in command line: {}", parse_result.message());
      return 1;
    }
    if (need_help) {
      std::cout << cli << '\n';
      return 0;
    }

    if (new_impl)
      return run<v2::Z80>();
    return run<v1::Z80>();
  }
  template<typename Z80Impl>
  int run() {
    auto sdl_state = sdl_init(); // RAII wrapper

    auto window = sdl_resource(SDL_CreateWindow("Specbolt ZX Spectrum Emulator", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, Video::Width, Video::Height, SDL_WINDOW_SHOWN));
    if (!window) {
      throw sdl_error("SDL_CreateWindow failed");
    }

    auto renderer = sdl_resource(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED));
    if (!renderer) {
      throw sdl_error("SDL_CreateRenderer failed");
    }

    auto texture = sdl_resource(SDL_CreateTexture(
        renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, Video::Width, Video::Height));
    if (!texture) {
      throw sdl_error("SDL_CreateTexture failed");
    }

    // TODO leaks
    using FillFunc = std::function<void(std::span<std::int16_t>)>;
    FillFunc fill_func;

    auto audio = sdl_audio{};

    Spectrum<Z80Impl> spectrum(rom, audio.freq());
    const v1::Disassembler dis{spectrum.memory()};

    if (!snapshot.empty()) {
      Snapshot::load(snapshot, spectrum.z80());
    }

    if (trace_instructions)
      spectrum.trace_next(trace_instructions);

    audio.callback = [&](const std::span<std::int16_t> buffer) {
      spectrum.audio().fill(spectrum.z80().cycle_count(), buffer);
    };

    audio.pause();

    bool quit = false;
    bool z80_running{true};
    auto next_print = std::chrono::high_resolution_clock::now() + std::chrono::seconds(1);
    auto next_frame = std::chrono::high_resolution_clock::now();
    while (!quit) {
      SDL_Event sdl_event{};
      while (SDL_PollEvent(&sdl_event) != 0) {
        switch (sdl_event.type) {
          case SDL_QUIT: quit = true; break;
          case SDL_KEYDOWN: spectrum.keyboard().key_down(sdl_event.key.keysym.sym); break;
          case SDL_KEYUP: spectrum.keyboard().key_up(sdl_event.key.keysym.sym); break;
          default: break;
        }
      }

      if (const auto now = std::chrono::high_resolution_clock::now(); now > next_frame) {
        if (z80_running) {
          const auto start_time = std::chrono::high_resolution_clock::now();
          try {
            const auto cycles_elapsed = spectrum.run_frame();
            const auto end_time = std::chrono::high_resolution_clock::now();
            const auto time_taken = end_time - start_time;
            const auto cycles_per_second =
                static_cast<double>(cycles_elapsed) /
                std::chrono::duration_cast<std::chrono::duration<double>>(time_taken).count();

            // probably better, but I'm now a mush
            // audio.queue(spectrum.audio().fill(spectrum.z80().cycle_count(), audio_buffer));

            if (end_time > next_print) {
              std::print(std::cout, "Virtual: {:.2f}MHz | lag {}\n", cycles_per_second / 1'000'000, now - next_frame);
              std::print(
                  std::cout, "Audio over/under: {}/{}\n", spectrum.audio().overruns(), spectrum.audio().underruns());
              next_print = end_time + std::chrono::seconds(1);
            }
          }
          catch (const std::exception &e) {
            std::print(std::cout, "Exception: {}\n", e.what());
            for (const auto &trace: spectrum.z80().history()) {
              trace.dump(std::cout, "  ");
              std::print(std::cout, "{}\n", dis.disassemble(trace.pc()).to_string());
            }
            spectrum.z80().dump();
            z80_running = false;
          }
        }

        void *pixels;
        int pitch;
        SDL_LockTexture(texture.get(), nullptr, &pixels, &pitch);
        const auto &video = spectrum.video();
        memcpy(pixels, video.screen().data(), video.screen().size_bytes());
        SDL_UnlockTexture(texture.get());

        SDL_RenderClear(renderer.get());
        SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
        SDL_RenderPresent(renderer.get());
        next_frame += std::chrono::milliseconds(20);
      }
    }
    return 0;
  }
};


} // namespace
} // namespace specbolt

int main(const int argc, const char *argv[]) {
  try {
    specbolt::SdlApp sdl;
    return sdl.Main(argc, argv);
  }
  catch (const std::exception &e) {
    std::cerr << "Fatal exception: " << e.what() << "\n";
    return 1;
  }
}
