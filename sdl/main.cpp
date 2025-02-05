#include <SDL.h>

#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "peripherals//Memory.hpp"
#include "z80/Disassembler.hpp"
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

const int WindowWidth = 640;
const int WindowHeight = 480;

void Main() {
  SdlInit sdl_init;

  std::unique_ptr<SDL_Window, decltype(SDL_DestroyWindow) *> window(
      SDL_CreateWindow("SDL Bitmap Display", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WindowWidth,
          WindowHeight, SDL_WINDOW_SHOWN),
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
      SDL_CreateTexture(
          renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WindowWidth, WindowHeight),
      SDL_DestroyTexture);
  if (!texture) {
    throw SdlError("SDL_CreateTexture failed");
  }

  bool quit = false;
  SDL_Event e;
  unsigned char rgb = 0xff;

  specbolt::Memory memory;
  memory.load("48.rom", 0, 16 * 1024);
  specbolt::Z80 z80(memory);
  const specbolt::Disassembler dis(memory);
  try {
    int trace{};
    for (int i = 0; i < 220'000; ++i) {
      const auto disassembled = dis.disassemble(z80.pc());
      if (trace)
        std::cout << disassembled.to_string() << "\n";
      z80.execute_one();
      // TODO update this to trace a specific address or whatever
      if (!trace && z80.regs().get(specbolt::RegisterFile::R16::HL) == 0x4000) {
        trace = 9999;
      }
      if (trace) {
        z80.dump();
        if (--trace == 0) {
          break;
        }
      }
    }
  }
  catch (const std::runtime_error &runtime_error) {
    std::print(std::cout, "Error: {}\n", runtime_error.what());
    z80.dump();
  }
  z80.dump();

  while (!quit) {
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        quit = true;
      }
    }

    void *pixels;
    int pitch;
    SDL_LockTexture(texture.get(), nullptr, &pixels, &pitch);
    memset(pixels, rgb, WindowWidth * WindowHeight * 4);
    SDL_UnlockTexture(texture.get());

    SDL_RenderClear(renderer.get());
    SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
    SDL_RenderPresent(renderer.get());

    rgb++;
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
