#include <SDL.h>

#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "peripherals/Memory.hpp"
#include "peripherals/Video.hpp"
#include "spectrum/Spectrum.hpp"
#include "z80/Z80.hpp"

namespace {

struct SdlError final : std::runtime_error {
  explicit SdlError(const std::string &message) : std::runtime_error(std::format("{}: {}", message, SDL_GetError())) {}
};

struct SdlInit {
  SdlInit() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      throw SdlError("SDL_Init failed");
    }
  }
  SdlInit(const SdlInit &) = delete;
  SdlInit &operator=(const SdlInit &) = delete;
  ~SdlInit() { SDL_Quit(); }
};

void Main() {
  SdlInit sdl_init;

  std::unique_ptr<SDL_Window, decltype(SDL_DestroyWindow) *> window(
      SDL_CreateWindow("Specbolt ZX Spectrum Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
          specbolt::Video::Width, specbolt::Video::Height, SDL_WINDOW_SHOWN),
      SDL_DestroyWindow);
  if (!window) {
    throw SdlError("SDL_CreateWindow failed");
  }

  std::unique_ptr<SDL_Renderer, decltype(SDL_DestroyRenderer) *> renderer(
      SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED), SDL_DestroyRenderer);
  if (!renderer) {
    throw SdlError("SDL_CreateRenderer failed");
  }

  const std::unique_ptr<SDL_Texture, decltype(SDL_DestroyTexture) *> texture(
      SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, specbolt::Video::Width,
          specbolt::Video::Height),
      SDL_DestroyTexture);
  if (!texture) {
    throw SdlError("SDL_CreateTexture failed");
  }

  bool quit = false;

  specbolt::Spectrum spectrum("48.rom");

  bool z80_running{true};
  auto next_print = std::chrono::high_resolution_clock::now() + std::chrono::seconds(1);
  while (!quit) {
    SDL_Event e{};
    while (SDL_PollEvent(&e) != 0) {
      switch (e.type) {
        case SDL_QUIT: quit = true; break;
        case SDL_KEYDOWN: spectrum.keyboard().key_down(e.key.keysym.sym); break;
        case SDL_KEYUP: spectrum.keyboard().key_up(e.key.keysym.sym); break;
        default: break;
      }
    }

    if (z80_running) {
      const auto start_time = std::chrono::high_resolution_clock::now();
      try {
        const auto cycles_elapsed = spectrum.run_frame();
        const auto end_time = std::chrono::high_resolution_clock::now();
        const auto time_taken = end_time - start_time;
        const auto cycles_per_second = static_cast<double>(cycles_elapsed) /
                                       std::chrono::duration_cast<std::chrono::duration<double>>(time_taken).count();
        if (end_time > next_print) {
          std::print(std::cout, "Cycles/sec: {:.2f}\n", cycles_per_second);
          next_print = end_time + std::chrono::seconds(1);
        }
      }
      catch (const std::exception &e) {
        std::print(std::cout, "Exception: {}\n", e.what());
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
    SDL_Delay(20);
  }
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
