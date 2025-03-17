#include "sdl_wrapper.hpp"

#include <format>

namespace specbolt {

sdl_error::sdl_error(const std::string &message) : std::runtime_error(std::format("{}: {}", message, SDL_GetError())) {}
sdl_init::sdl_init() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
    throw sdl_error("SDL_Init failed");
  }
}

sdl_init::~sdl_init() { SDL_Quit(); }

void sdl_destructor::operator()(SDL_Window *ptr) const noexcept { SDL_DestroyWindow(ptr); }
void sdl_destructor::operator()(SDL_Renderer *ptr) const noexcept { SDL_DestroyRenderer(ptr); }
void sdl_destructor::operator()(SDL_Texture *ptr) const noexcept { SDL_DestroyTexture(ptr); }

void sdl_audio::disabled_callback(std::span<std::int16_t>) {
  // do nothing
}

void sdl_audio::audio_callback(void *vo, std::uint8_t *stream, const int len) {
  const auto &self = *static_cast<sdl_audio *>(vo);
  self.callback(std::span{reinterpret_cast<std::int16_t *>(stream), static_cast<std::size_t>(len / 2)});
}

sdl_audio::sdl_audio(const audio_settings settings) {
  const SDL_AudioSpec desired{
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
    throw sdl_error("Unable to initialise sound");
  }
}

void sdl_audio::queue(const std::span<std::int16_t> buffer) noexcept {
  if (id.has_value()) {
    SDL_QueueAudio(*id, buffer.data(), static_cast<std::uint32_t>(buffer.size_bytes()));
  }
}

sdl_audio::~sdl_audio() noexcept {
  if (id.has_value()) {
    SDL_CloseAudioDevice(*id);
  }
}

} // namespace specbolt
