#include "sdl_wrapper.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>

#include <lyra/lyra.hpp>

#ifdef SPECBOLT_MODULES
import peripherals;
import spectrum;
import z80_v1;
import z80_v2;
import z80_v3;
#else
#include "peripherals/Video.hpp"
#include "spectrum/Assets.hpp"
#include "spectrum/Snapshot.hpp"
#include "spectrum/Spectrum.hpp"
#include "z80/v1/Disassembler.hpp"
#include "z80/v1/Z80.hpp"
#include "z80/v2/Z80.hpp"
#include "z80/v3/Z80.hpp"
#endif


namespace specbolt {

namespace {

constexpr SDL_Rect calc_rect(const int width, const int height) {
  static constexpr auto ideal_aspect_ratio =
      static_cast<double>(Video::VisibleWidth) / static_cast<double>(Video::VisibleHeight);
  if (static_cast<double>(width) / static_cast<double>(height) > ideal_aspect_ratio) {
    const auto required_width = static_cast<int>(height * ideal_aspect_ratio);
    return {(width - required_width) / 2, 0, required_width, height};
  }
  const auto required_height = static_cast<int>(width / ideal_aspect_ratio);
  return {0, (height - required_height) / 2, width, required_height};
}

struct SdlApp {
  std::filesystem::path rom;
  std::filesystem::path snapshot;
  std::filesystem::path tape;
  bool need_help{};
  std::size_t trace_instructions{};
  int impl{1};
  double video_refresh_rate{50};
  double emulator_speed{1};
  double zoom{4};
  bool spec128{};

  int Main(const int argc, const char *argv[]) {
    const auto cli = lyra::cli() //
                     | lyra::help(need_help) //
                     | lyra::opt(spec128)["--128"]("Use the 128K Spectrum") //
                     | lyra::opt(rom, "ROM")["--rom"]("Where to find the ROM") //
                     | lyra::opt(trace_instructions, "NUM")["--trace"]("Trace the first NUM instructions") //
                     | lyra::opt(impl, "impl")["--impl"]("Use the specified implementation.") //
                     | lyra::opt(video_refresh_rate, "HZ")["--video-refresh"]("Refresh the video at HZ") //
                     | lyra::opt(emulator_speed, "X")["--emulator-speed"]("Multiplier on emulation speed") //
                     | lyra::opt(zoom, "X")["--zoom"]("Multiplier on display zoom") //
                     | lyra::opt(tape, "TAPE")["--tape"]("Queue up TAPE") //
                     | lyra::arg(snapshot, "SNAPSHOT")("Snapshot to load");
    if (const auto parse_result = cli.parse({argc, argv}); !parse_result) {
      std::println(std::cerr, "Error in command line: {}", parse_result.message());
      return 1;
    }
    if (need_help) {
      std::cout << cli << '\n';
      return 0;
    }

    if (rom.empty()) {
      rom = get_asset_dir() / (spec128 ? "128.rom" : "48.rom");
    }

    switch (impl) {
      case 1: return run<v1::Z80>();
      case 2: return run<v2::Z80>();
      case 3: return run<v3::Z80>();
      default: break;
    }
    std::print(std::cerr, "Bad implementation {}\n", impl);
    return 1;
  }
  template<typename Z80Impl>
  int run() {
    auto sdl_state = sdl_init(); // RAII wrapper

    auto window = sdl_resource(SDL_CreateWindow("Specbolt ZX Spectrum Emulator", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, static_cast<int>(zoom * Video::VisibleWidth),
        static_cast<int>(zoom * Video::VisibleHeight), SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE));
    if (!window) {
      throw sdl_error("SDL_CreateWindow failed");
    }

    auto renderer = sdl_resource(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED));
    if (!renderer) {
      throw sdl_error("SDL_CreateRenderer failed");
    }

    auto texture = sdl_resource(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        Video::VisibleWidth, Video::VisibleHeight));
    if (!texture) {
      throw sdl_error("SDL_CreateTexture failed");
    }

    constexpr auto desired_freq = 44'100;
    const auto samples = static_cast<std::uint16_t>(desired_freq / video_refresh_rate);

    auto audio = sdl_audio{{.frequency = desired_freq, .format = AUDIO_S16SYS, .channels = 1, .samples = samples}};
    audio.pause(false);

    Spectrum<Z80Impl> spectrum(spec128 ? Variant::Spectrum128 : Variant::Spectrum48, rom,
        static_cast<std::size_t>(audio.freq()), emulator_speed);
    const v1::Disassembler dis{spectrum.memory()};

    if (!snapshot.empty()) {
      Snapshot::load(snapshot, spectrum.z80());
    }

    if (!tape.empty()) {
      spectrum.tape().load(tape);
    }

    if (trace_instructions)
      spectrum.trace_next(trace_instructions);

    bool quit = false;
    bool z80_running{true};
    auto next_print = std::chrono::high_resolution_clock::now() + std::chrono::seconds(1);
    auto next_emu_frame = std::chrono::high_resolution_clock::now();
    auto next_display_frame = std::chrono::high_resolution_clock::now();
    const auto video_delay = std::chrono::microseconds(static_cast<unsigned long>(1'000'000 / video_refresh_rate));
    const auto emulator_delay = std::chrono::microseconds(static_cast<unsigned long>(20'000 / emulator_speed));
    while (!quit) {
      SDL_Event sdl_event{};
      while (SDL_PollEvent(&sdl_event) != 0) {
        switch (sdl_event.type) {
          case SDL_QUIT: quit = true; break;
          case SDL_KEYDOWN: {
            if (sdl_event.key.keysym.sym == SDLK_F1)
              spectrum.play();
            spectrum.keyboard().key_down(sdl_event.key.keysym.sym);
            break;
          }
          case SDL_KEYUP: spectrum.keyboard().key_up(sdl_event.key.keysym.sym); break;
          default: break;
        }
      }

      const auto now = std::chrono::high_resolution_clock::now();
      if (now > next_emu_frame) {
        if (z80_running) {
          const auto start_time = std::chrono::high_resolution_clock::now();
          try {
            const auto cycles_elapsed = spectrum.run_frame();
            const auto end_time = std::chrono::high_resolution_clock::now();
            const auto time_taken = end_time - start_time;
            const auto cycles_per_second =
                static_cast<double>(cycles_elapsed) /
                std::chrono::duration_cast<std::chrono::duration<double>>(time_taken).count();

            audio.queue(spectrum.audio().end_frame(spectrum.z80().cycle_count()));

            if (end_time > next_print) {
              std::print(
                  std::cout, "Virtual: {:.2f}MHz | lag {}\n", cycles_per_second / 1'000'000, now - next_emu_frame);
              next_print = end_time + std::chrono::seconds(1);
            }
          }
          catch (const std::exception &e) {
            std::print(std::cout, "Exception: {}\n", e.what());
            for (const auto &trace: spectrum.history()) {
              trace.dump(std::cout, "  ");
              std::print(std::cout, "{}\n", dis.disassemble(trace.pc()).to_string());
            }
            spectrum.z80().dump();
            z80_running = false;
          }
        }
        next_emu_frame += emulator_delay;
      }
      if (now > next_display_frame) {
        void *pixels;
        int pitch;
        SDL_LockTexture(texture.get(), nullptr, &pixels, &pitch);
        spectrum.video().blit_to(
            std::span(static_cast<std::uint32_t *>(pixels), Video::VisibleWidth * Video::VisibleHeight));
        SDL_UnlockTexture(texture.get());

        int w{}, h{};
        SDL_GetWindowSize(window.get(), &w, &h);
        const auto dest_rect = calc_rect(w, h);
        SDL_RenderClear(renderer.get());
        SDL_RenderCopy(renderer.get(), texture.get(), nullptr, &dest_rect);
        SDL_RenderPresent(renderer.get());
        next_display_frame += video_delay;
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
