#include "Z80.hpp"

namespace specbolt {

void Z80::execute_one() {
  const auto pc = regs_.pc();
  regs_.pc(pc + 1);
  switch (memory_[pc]) {
    case 0: // NOP
      break;
  }
}

} // namespace specbolt
