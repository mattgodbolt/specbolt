#pragma once

#include <SDL.h>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>

// I know it should be implemented somewhere else, but it doesn't matter much

struct sdl_error final : std::runtime_error {
  explicit sdl_error(const std::string &message) : std::runtime_error(std::format("{}: {}", message, SDL_GetError())) {}
};

struct sdl_init {
  sdl_init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
      throw sdl_error("SDL_Init failed");
    }
  }
  sdl_init(const sdl_init &) = delete;
  sdl_init &operator=(const sdl_init &) = delete;
  ~sdl_init() { SDL_Quit(); }
};

struct sdl_destructor {
  void operator()(SDL_Window *ptr) const noexcept { SDL_DestroyWindow(ptr); }
  void operator()(SDL_Renderer *ptr) const noexcept { SDL_DestroyRenderer(ptr); }
  void operator()(SDL_Texture *ptr) const noexcept { SDL_DestroyTexture(ptr); }
};

struct audio_settings {
  int frequency = 16000;
  SDL_AudioFormat format = AUDIO_S16;
  Uint8 channels = 1;
  Uint16 samples = 4096;
};

struct sdl_audio {
  static void disabled_callback(std::span<std::int16_t>) {
    // do nothing
  }

  std::optional<SDL_AudioDeviceID> id;
  SDL_AudioSpec obtained;
  std::function<void(std::span<std::int16_t>)> callback = disabled_callback;


  static void audio_callback(void *vo, Uint8 *stream, const int len) {
    auto *self = static_cast<sdl_audio *>(vo);
    self->callback(std::span{reinterpret_cast<std::int16_t *>(stream), static_cast<std::size_t>(len / 2)});
  }

  sdl_audio(audio_settings settings = {}) {
    SDL_AudioSpec desired{
        .freq = settings.frequency,
        .format = settings.format,
        .channels = settings.channels,
        .silence = false,
        .samples = settings.samples,
        .padding = 0,
        .size = 0,
        .callback = audio_callback,
        .userdata = this,
    };

    id = SDL_OpenAudioDevice(
        nullptr, 0, &desired, &obtained, SDL_AUDIO_ALLOW_SAMPLES_CHANGE | SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (id == 0) {
      throw sdl_error("Unable to initialise sound: " + std::string(SDL_GetError()));
    }
  }

  sdl_audio(const sdl_audio &) = delete;
  sdl_audio(sdl_audio &&) = delete;

  int freq() const noexcept { return obtained.freq; }

  void pause() noexcept {
    if (id.has_value()) {
      SDL_PauseAudioDevice(*id, false);
    }
  }

  void queue(std::span<std::int16_t> buffer) noexcept {
    // FIXME
    if (id.has_value()) {
      SDL_QueueAudio(*id, buffer.data(), static_cast<uint32_t>(buffer.size()));
    }
  }

  ~sdl_audio() noexcept {
    if (id.has_value()) {
      SDL_CloseAudioDevice(*id);
    }
  }
};

template<typename T>
constexpr auto sdl_resource(T *value) noexcept {
  return std::unique_ptr<T, sdl_destructor>(value);
}
