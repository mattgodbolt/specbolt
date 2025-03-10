#include <SDL.h>
#include <chrono>
#include <filesystem>
#include <iostream>

#include <lyra/lyra.hpp>

import spectrum;
import sdl_wrapper;

int main(int argc, char *argv[]) try {
  std::filesystem::path rom{"48.rom"};
  std::filesystem::path snapshot;
  bool need_help{};
  std::size_t trace_instructions{};
  bool new_impl{};

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

  auto sdl_state = sdl_init(); // RAII wrapper

  auto window = sdl_resource(SDL_CreateWindow("Specbolt ZX Spectrum Emulator", SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED, specbolt::Video::Width, specbolt::Video::Height, SDL_WINDOW_SHOWN));
  if (!window) {
    throw sdl_error("SDL_CreateWindow failed");
  }

  auto renderer = sdl_resource(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED));
  if (!renderer) {
    throw sdl_error("SDL_CreateRenderer failed");
  }

  auto texture = sdl_resource(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      specbolt::Video::Width, specbolt::Video::Height));
  if (!texture) {
    throw sdl_error("SDL_CreateTexture failed");
  }

  // TODO leaks
  using FillFunc = std::function<void(std::span<std::int16_t>)>;
  FillFunc fill_func;

  auto audio = sdl_audio{};

  specbolt::Spectrum spectrum(rom, audio.freq(), new_impl);
  const specbolt::Disassembler dis{spectrum.memory()};

  if (!snapshot.empty()) {
    specbolt::Snapshot::load(snapshot, spectrum.z80());
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
          const auto cycles_per_second = static_cast<double>(cycles_elapsed) /
                                         std::chrono::duration_cast<std::chrono::duration<double>>(time_taken).count();

          // probably better, but I'm now a mush
          // audio.queue(spectrum.audio().fill(spectrum.z80().cycle_count(), audio_buffer));

          if (end_time > next_print) {
            std::println("Cycles/sec: {:.2f} | lag {}", cycles_per_second, now - next_frame);
            std::println("Audio over/under: {}/{}", spectrum.audio().overruns(), spectrum.audio().underruns());
            next_print = end_time + std::chrono::seconds(1);
          }
        }
        catch (const std::exception &e) {
          std::print(std::cout, "Exception: {}\n", e.what());
          for (const auto &trace: spectrum.z80().history()) {
            trace.dump(std::cout, "  ");
            std::println("{}", dis.disassemble(trace.pc()).to_string());
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
catch (const std::exception &e) {
  std::println(std::cerr, "Fatal exception: {}", e.what());
  return 1;
}
