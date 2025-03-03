#include "Snapshot.hpp"

#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>

namespace specbolt {

namespace {

struct [[gnu::packed]] SnapshotHeader {
  std::uint8_t i;
  std::uint16_t hl_;
  std::uint16_t de_;
  std::uint16_t bc_;
  std::uint16_t af_;
  std::uint16_t hl;
  std::uint16_t de;
  std::uint16_t bc;
  std::uint16_t iy;
  std::uint16_t ix;
  std::uint8_t iff2;
  std::uint8_t r;
  std::uint16_t af;
  std::uint16_t sp;
  std::uint8_t irq_mode;
  std::uint8_t border_colour;
};

static_assert(offsetof(SnapshotHeader, i) == 0);
static_assert(offsetof(SnapshotHeader, hl_) == 1);
static_assert(offsetof(SnapshotHeader, ix) == 0x11);
static_assert(offsetof(SnapshotHeader, border_colour) == 0x1a);
static_assert(sizeof(SnapshotHeader) == 27);

constexpr auto RamSnapshotSize = 48 * 1024;
constexpr auto ExpectedSize = RamSnapshotSize + sizeof(SnapshotHeader);

} // namespace

void Snapshot::load(const std::filesystem::path &snapshot, Z80 &z80) {
  if (const auto fs = file_size(snapshot); fs != ExpectedSize) {
    throw std::runtime_error(
        std::format("File size for {} is not as expected ({} vs {})", snapshot.string(), fs, ExpectedSize));
  }
  std::ifstream load_stream(snapshot, std::ios::binary);
  if (!load_stream) {
    throw std::runtime_error(std::format("Failed to open file '{}': {}", snapshot.string(), std::strerror(errno)));
  }

  SnapshotHeader header;
  load_stream.read(reinterpret_cast<char *>(&header), sizeof(header));
  std::vector<std::uint8_t> memory(RamSnapshotSize);
  load_stream.read(reinterpret_cast<char *>(memory.data()), RamSnapshotSize);
  if (!load_stream)
    throw std::runtime_error(std::format("Unable to read file '{}'", snapshot.string()));

  for (auto i = 0u; i < memory.size(); ++i)
    z80.memory().write(static_cast<std::uint16_t>(16384 + i), memory[i]);

  auto &registers = z80.registers();
  registers.set(RegisterFile::R16::AF, header.af);
  registers.set(RegisterFile::R16::BC, header.bc);
  registers.set(RegisterFile::R16::DE, header.de);
  registers.set(RegisterFile::R16::HL, header.hl);
  registers.set(RegisterFile::R16::AF_, header.af_);
  registers.set(RegisterFile::R16::BC_, header.bc_);
  registers.set(RegisterFile::R16::DE_, header.de_);
  registers.set(RegisterFile::R16::HL_, header.hl_);
  registers.set(RegisterFile::R16::IX, header.ix);
  registers.set(RegisterFile::R16::IY, header.iy);
  registers.sp(header.sp);

  registers.i(header.i);
  registers.r(header.r);

  z80.iff2(header.iff2);
  z80.irq_mode(header.irq_mode);
  z80.out(0xfe, header.border_colour);
  z80.retn();
}

} // namespace specbolt
