#ifndef SPECBOLT_MODULES
#include "spectrum/Snapshot.hpp"
#include "peripherals/Memory.hpp"
#include "spectrum/Spectrum.hpp"
#include "z80/common/Flags.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <vector>
#endif

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
constexpr auto SnaExpectedSize = RamSnapshotSize + sizeof(SnapshotHeader);

struct [[gnu::packed]] Z80Header {
  std::uint8_t a;
  std::uint8_t f;
  std::uint8_t c;
  std::uint8_t b;
  std::uint8_t l;
  std::uint8_t h;
  std::uint8_t pc_l;
  std::uint8_t pc_h;
  std::uint8_t sp_l;
  std::uint8_t sp_h;
  std::uint8_t i;
  std::uint8_t r;
  std::uint8_t flag1;
  std::uint8_t e;
  std::uint8_t d;
  std::uint8_t c_;
  std::uint8_t b_;
  std::uint8_t e_;
  std::uint8_t d_;
  std::uint8_t l_;
  std::uint8_t h_;
  std::uint8_t a_;
  std::uint8_t f_;
  std::uint8_t iy_l;
  std::uint8_t iy_h;
  std::uint8_t ix_l;
  std::uint8_t ix_h;
  std::uint8_t iff1;
  std::uint8_t iff2;
  std::uint8_t flag2;
};
static_assert(offsetof(Z80Header, a_) == 21);
static_assert(sizeof(Z80Header) == 30);

struct [[gnu::packed]] Z80HeaderV2 {
  std::uint16_t pc;
  std::uint8_t hw_mode;
  std::uint8_t samram;
  std::uint8_t i1paged;
  std::uint8_t emu_flags;
  std::uint8_t last_port_fffd;
  std::array<std::uint8_t, 16> sound_chip_regs;
};
static_assert(sizeof(Z80HeaderV2) == 23);
struct [[gnu::packed]] Z80HeaderV3 : Z80HeaderV2 {
  std::uint16_t low_ts;
  std::uint8_t hight_ts;
  std::uint8_t spectator_flags;
  std::uint8_t mgt_flags;
  std::uint8_t multiface_flags;
  std::uint8_t ramflag_0;
  std::uint8_t ramflag_8192;
  std::array<std::uint8_t, 10> joystick_mapping;
  std::array<std::uint8_t, 10> ascii_mapping;
  std::uint8_t mgt_type;
  std::uint8_t disciple_inhibit_button;
  std::uint8_t disciple_inhibit_flag;
};
static_assert(sizeof(Z80HeaderV3) == 54);
struct [[gnu::packed]] Z80HeaderV31 : Z80HeaderV3 {
  std::uint8_t last_port_1ffd;
};
static_assert(sizeof(Z80HeaderV31) == 55);

std::vector<std::uint8_t> z80_decompress(const std::vector<std::uint8_t> &input) {
  std::vector<std::uint8_t> output;
  output.reserve(16 * 1024);
  for (auto i = 0u; i < input.size(); ++i) {
    if (const auto b = input[i]; i < input.size() - 4 && b == 0xed && input[i + 1] == 0xed) {
      const auto count = input[i + 2];
      const auto value = input[i + 3];
      for (auto j = 0u; j < count; ++j)
        output.push_back(value);
      i += 3;
    }
    else {
      output.push_back(b);
    }
  }
  return output;
}

std::vector<std::uint8_t> read_to_end(std::ifstream &input) {
  std::vector<std::uint8_t> output;
  const auto pos = input.tellg();
  input.seekg(0, std::ios::end);
  output.resize(static_cast<std::size_t>(input.tellg() - pos));
  input.seekg(pos, std::ios::beg);
  input.read(reinterpret_cast<char *>(output.data()), static_cast<std::streamsize>(output.size()));
  return output;
}

} // namespace

void Snapshot::load_sna(const std::filesystem::path &snapshot, Z80Base &z80) {
  if (const auto fs = file_size(snapshot); fs != SnaExpectedSize) {
    throw std::runtime_error(
        std::format("File size for {} is not as expected ({} vs {})", snapshot.string(), fs, SnaExpectedSize));
  }
  std::ifstream load_stream(snapshot, std::ios::binary);
  if (!load_stream) {
    throw std::runtime_error(std::format("Failed to open file '{}': {}", snapshot.string(), std::strerror(errno)));
  }

  SnapshotHeader header{};
  load_stream.read(reinterpret_cast<char *>(&header), sizeof(header));
  std::vector<std::uint8_t> memory(RamSnapshotSize);
  load_stream.read(reinterpret_cast<char *>(memory.data()), RamSnapshotSize);
  if (!load_stream)
    throw std::runtime_error(std::format("Unable to read file '{}'", snapshot.string()));

  for (auto i = 0u; i < memory.size(); ++i)
    z80.memory().write(static_cast<std::uint16_t>(16384 + i), memory[i]);

  auto &registers = z80.regs();
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

  // Simulate a retn.
  z80.iff1(z80.iff2());
  z80.regs().pc(z80.memory().read16(z80.regs().sp()));
  z80.regs().sp(static_cast<std::uint16_t>(z80.regs().sp() + 2));
}

void Snapshot::load_z80(const std::filesystem::path &snapshot, Z80Base &z80) {
  std::ifstream load_stream(snapshot, std::ios::binary);
  if (!load_stream) {
    throw std::runtime_error(std::format("Failed to open file '{}': {}", snapshot.string(), std::strerror(errno)));
  }

  Z80Header header{};
  load_stream.read(reinterpret_cast<char *>(&header), sizeof(header));
  if (!load_stream)
    throw std::runtime_error(std::format("Unable to read file '{}'", snapshot.string()));
  if (header.flag1 == 255)
    header.flag1 = 1; // Compatibility with some old snapshots.

  auto &registers = z80.regs();
  registers.set(RegisterFile::R8::A, header.a);
  registers.set(RegisterFile::R8::F, header.f);
  registers.set(RegisterFile::R8::B, header.b);
  registers.set(RegisterFile::R8::C, header.c);
  registers.set(RegisterFile::R8::D, header.d);
  registers.set(RegisterFile::R8::E, header.e);
  registers.set(RegisterFile::R8::H, header.h);
  registers.set(RegisterFile::R8::L, header.l);
  registers.set(RegisterFile::R8::A_, header.a_);
  registers.set(RegisterFile::R8::F_, header.f_);
  registers.set(RegisterFile::R8::B_, header.b_);
  registers.set(RegisterFile::R8::C_, header.c_);
  registers.set(RegisterFile::R8::D_, header.d_);
  registers.set(RegisterFile::R8::E_, header.e_);
  registers.set(RegisterFile::R8::H_, header.h_);
  registers.set(RegisterFile::R8::L_, header.l_);

  registers.set(RegisterFile::R8::IXH, header.ix_h);
  registers.set(RegisterFile::R8::IXL, header.ix_l);
  registers.set(RegisterFile::R8::IYH, header.iy_h);
  registers.set(RegisterFile::R8::IYL, header.iy_l);

  registers.sp(static_cast<std::uint16_t>(header.sp_h << 8 | header.sp_l));

  registers.i(header.i);
  registers.r((header.r & 0x7f) | (header.flag1 & 0x80));
  z80.out(0xfe, (header.flag1 >> 1) & 0x07);

  z80.iff1(header.iff1);
  z80.iff2(header.iff2);
  z80.irq_mode(header.flag2 & 0x3);

  if (header.pc_h || header.pc_l) {
    z80.regs().pc(static_cast<std::uint16_t>(header.pc_h << 8 | header.pc_l));
    if (header.flag1 & (1 << 5)) {
      // Compressed...
      auto compressed = read_to_end(load_stream);
      if (!load_stream)
        throw std::runtime_error(std::format("Unable to read file '{}'", snapshot.string()));
      const auto size = compressed.size();
      if (size < 4 || compressed[size - 4] != 0 || compressed[size - 3] != 0xed || compressed[size - 2] != 0xed ||
          compressed[size - 1] != 0)
        throw std::runtime_error(std::format("Unable to read file '{}' (bad trailer)", snapshot.string()));
      compressed.resize(size - 4);
      const auto uncompressed = z80_decompress(compressed);
      if (uncompressed.size() != 48 * 1024)
        throw std::runtime_error(std::format("Unable to read file '{}'", snapshot.string()));
      for (auto i = 0u; i < uncompressed.size(); ++i)
        z80.memory().write(static_cast<std::uint16_t>(16384 + i), uncompressed[i]);
    }
    else {
      std::vector<std::uint8_t> memory(RamSnapshotSize);
      load_stream.read(reinterpret_cast<char *>(memory.data()), RamSnapshotSize);
      if (!load_stream)
        throw std::runtime_error(std::format("Unable to read file '{}'", snapshot.string()));

      for (auto i = 0u; i < memory.size(); ++i)
        z80.memory().write(static_cast<std::uint16_t>(16384 + i), memory[i]);
    }
  }
  else {
    // Version 2 or 3.
    std::uint16_t header_len{};
    load_stream.read(reinterpret_cast<char *>(&header_len), sizeof(header_len));
    if (!load_stream)
      throw std::runtime_error(std::format("Unable to read file '{}'", snapshot.string()));
    auto variant{Variant::Spectrum48}; // TODO return this somehow?
    auto handle_load = [&]<typename HeaderType> {
      HeaderType extended_header{};
      load_stream.read(reinterpret_cast<char *>(&extended_header), sizeof(extended_header));
      if (!load_stream)
        throw std::runtime_error(std::format("Unable to read file '{}'", snapshot.string()));
      z80.regs().pc(extended_header.pc);
      variant = extended_header.hw_mode == 0 ? Variant::Spectrum48 : Variant::Spectrum128; // TODO not this
    };
    switch (header_len) {
      case sizeof(Z80HeaderV2): handle_load.operator()<Z80HeaderV2>(); break;
      case sizeof(Z80HeaderV3): handle_load.operator()<Z80HeaderV3>(); break;
      case sizeof(Z80HeaderV31): handle_load.operator()<Z80HeaderV31>(); break;
      default: throw std::runtime_error(std::format("Unsupported header length: {}", header_len));
    }

    for (;;) {
      std::uint16_t size{};
      std::uint8_t page{};
      load_stream.read(reinterpret_cast<char *>(&size), sizeof(size));
      load_stream.read(reinterpret_cast<char *>(&page), sizeof(page));
      if (load_stream.eof())
        break;
      if (!load_stream)
        throw std::runtime_error(std::format("Unable to read file '{}'", snapshot.string()));
      std::vector<std::uint8_t> chunk(size == 0xffff ? 16384 : size);
      load_stream.read(reinterpret_cast<char *>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
      if (!load_stream)
        throw std::runtime_error(std::format("Unable to read file '{}'", snapshot.string()));
      if (size != 0xffff)
        chunk = z80_decompress(chunk);
      if (chunk.size() > 16384)
        throw std::runtime_error(std::format("Unable to read file '{}' - decompression failed", snapshot.string()));
      // TODO sometimes chunks are smaller, should we zero the rest?
      // TODO not this, doesn't support 48k mode
      const auto ram_bank = static_cast<std::uint8_t>(page >= 3 ? page - 3 : page + 8);
      for (auto i = 0u; i < chunk.size(); ++i)
        z80.memory().raw_write_checked(ram_bank, static_cast<std::uint16_t>(i), chunk[i]);
    }
  }
}

void Snapshot::load(const std::filesystem::path &snapshot, Z80Base &z80) {
  if (snapshot.extension() == ".z80")
    return load_z80(snapshot, z80);
  if (snapshot.extension() == ".sna")
    return load_sna(snapshot, z80);
  throw std::runtime_error(std::format("Unsupported snapshot format: {}", snapshot.string()));
}

} // namespace specbolt
