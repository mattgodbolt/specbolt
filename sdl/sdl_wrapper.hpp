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
  static void disabled_callback(std::span<std::int16_t>);

  std::optional<SDL_AudioDeviceID> id;
  SDL_AudioSpec obtained;
  std::function<void(std::span<std::int16_t>)> callback = disabled_callback;

  static void audio_callback(void *vo, std::uint8_t *stream, int len);

  explicit sdl_audio(audio_settings settings = {});

  sdl_audio(const sdl_audio &) = delete;
  sdl_audio(sdl_audio &&) = delete;

  int freq() const noexcept { return obtained.freq; }

  void pause() const noexcept {
    if (id.has_value()) {
      SDL_PauseAudioDevice(*id, false);
    }
  }

  void queue(std::span<std::int16_t> buffer) noexcept;

  ~sdl_audio() noexcept;
};

template<typename T>
constexpr auto sdl_resource(T *value) noexcept {
  return std::unique_ptr<T, sdl_destructor>(value);
}

} // namespace specbolt
