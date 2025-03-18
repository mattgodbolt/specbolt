#include "peripherals/Memory.hpp"

extern "C" [[clang::export_name("test")]] int test() {
  const specbolt::Memory memory{4};
  return memory.read(0);
}
