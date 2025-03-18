#include "peripherals/Video.hpp"
#include "spectrum/Spectrum.hpp"
#include "z80/v2/Z80.hpp"

#include <cstdint>
#include <vector>

extern "C" void __cxa_allocate_exception() { /*TODO*/ }
extern "C" void __cxa_throw(const void *p, const std::type_info *tinfo, void (*)(void *)) {
  fprintf(stderr, "*** C++ exception (%s) thrown ***\n", tinfo->name());
  // Bit of a hack assumes we only throw sane exceptions...
  fprintf(stderr, "*** C++ exception: %s ***\n", static_cast<const std::exception *>(p)->what());
  abort();
}

struct WebSpectrum {
  specbolt::Spectrum<specbolt::v2::Z80> spectrum{specbolt::Variant::Spectrum48, "assets/48.rom", 16000};
  std::vector<std::uint32_t> frame;
  WebSpectrum(const specbolt::Variant variant, const char *rom, const int audio_sample_rate) :
      spectrum(variant, rom, audio_sample_rate) {
    frame.resize(specbolt::Video::VisibleHeight * specbolt::Video::VisibleWidth);
  }
};

extern "C" [[clang::export_name("create")]] WebSpectrum *create(const int variant, const int audio_sample_rate) {
  printf("Variant = %d\n", variant);
  switch (variant) {
    case 48: return new WebSpectrum(specbolt::Variant::Spectrum48, "assets/48.rom", audio_sample_rate);
    case 128: return new WebSpectrum(specbolt::Variant::Spectrum128, "assets/128.rom", audio_sample_rate);
    default: break;
  }
  return nullptr;
}

extern "C" [[clang::export_name("run_frame")]] void run_frame(WebSpectrum &ws) { ws.spectrum.run_frame(); }

extern "C" [[clang::export_name("render_video")]] void *render_video(WebSpectrum &ws) {
  ws.spectrum.video().blit_to(ws.frame, true);
  return ws.frame.data();
}

extern "C" [[clang::export_name("video_height")]] std::size_t video_height() { return specbolt::Video::VisibleHeight; }
extern "C" [[clang::export_name("video_width")]] std::size_t video_width() { return specbolt::Video::VisibleWidth; }

extern "C" [[clang::export_name("key_state")]] void key_state(
    WebSpectrum &ws, const std::int32_t key_code, const bool pressed_or_released) {
  auto &kb = ws.spectrum.keyboard();
  if (pressed_or_released)
    kb.key_down(key_code);
  else
    kb.key_up(key_code);
}

int main() { /* placates wasm.start() */ }
