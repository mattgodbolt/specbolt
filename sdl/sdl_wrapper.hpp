#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>

#include <SDL.h>

namespace specbolt {

struct sdl_error final : std::runtime_error {
  explicit sdl_error(const std::string &message);
};

struct sdl_init {
  sdl_init();
  sdl_init(const sdl_init &) = delete;
  sdl_init &operator=(const sdl_init &) = delete;
  ~sdl_init();
};

struct sdl_destructor {
  void operator()(SDL_Window *ptr) const noexcept;
  void operator()(SDL_Renderer *ptr) const noexcept;
  void operator()(SDL_Texture *ptr) const noexcept;
};

struct audio_settings {
  int frequency = 16000;
  SDL_AudioFormat format = AUDIO_S16;
  std::uint8_t channels = 1;
  std::uint16_t samples = 4096;
};

struct sdl_audio {
  std::optional<SDL_AudioDeviceID> id;
  SDL_AudioSpec obtained;

  explicit sdl_audio(audio_settings settings = {});

  sdl_audio(const sdl_audio &) = delete;
  sdl_audio(sdl_audio &&) = delete;

  [[nodiscard]] int freq() const noexcept { return obtained.freq; }

  void pause(const bool paused = true) const;

  void queue(std::span<const std::int16_t> buffer);

  ~sdl_audio() noexcept;
};

template<typename T>
constexpr auto sdl_resource(T *value) noexcept {
  return std::unique_ptr<T, sdl_destructor>(value);
}

} // namespace specbolt
