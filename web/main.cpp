#include "spectrum/Spectrum.hpp"
#include "z80/v2/Z80.hpp"

extern "C" void __cxa_allocate_exception() { abort(); }
extern "C" void __cxa_throw() { abort(); }

extern "C" [[clang::export_name("test")]] int test() {
  const specbolt::Spectrum<specbolt::v2::Z80> spectrum(specbolt::Variant::Spectrum48, "48.rom", 16000);
  return spectrum.memory().read(0);
}
