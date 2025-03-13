#ifndef SPECBOLT_MODULES
#include "z80/v2/Z80Impl.hpp"
#endif

namespace specbolt::v2::impl {

void decode_and_run_cb(Z80 &z80) {
  const auto opcode = z80.read_opcode();
  cb_table<build_execute_hl>[opcode](z80);
}

void decode_and_run_ed(Z80 &z80) {
  const auto opcode = z80.read_opcode();
  ed_table<build_execute>[opcode](z80);
}

void decode_and_run_dd(Z80 &z80) {
  const auto opcode = z80.read_opcode();
  dd_table<build_execute_ixiy<RegisterFile::R16::IX>>[opcode](z80);
}

void decode_and_run_fd(Z80 &z80) {
  const auto opcode = z80.read_opcode();
  fd_table<build_execute_ixiy<RegisterFile::R16::IY>>[opcode](z80);
}

void decode_and_run_ddcb(Z80 &z80) {
  const auto offset = static_cast<std::int8_t>(z80.read_immediate());
  z80.pass_time(1); // TODO this?
  const auto address = static_cast<std::uint16_t>(z80.regs().get(RegisterFile::R16::IX) + offset);
  z80.regs().wz(address);
  const auto opcode = z80.read_opcode();
  ddcb_table<build_execute>[opcode](z80);
}

void decode_and_run_fdcb(Z80 &z80) {
  const auto offset = static_cast<std::int8_t>(z80.read_immediate());
  z80.pass_time(1); // TODO this?
  const auto address = static_cast<std::uint16_t>(z80.regs().get(RegisterFile::R16::IY) + offset);
  z80.regs().wz(address);
  // Fetch the next opcode.
  const auto opcode = z80.read_opcode();
  fdcb_table<build_execute>[opcode](z80);
}

// TODO the fdcb and ddcb tables miss out on the duplicated encodings and the `res 0,(ix+d,b)` type instructions.
//   Hopefully won't matter for now...

} // namespace specbolt::v2::impl
