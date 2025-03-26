#pragma once

#ifndef SPECBOLT_MODULES
#include "peripherals/Audio.hpp"
#include "peripherals/Keyboard.hpp"
#include "peripherals/Memory.hpp"
#include "peripherals/Tape.hpp"
#include "peripherals/Video.hpp"

#include "z80/common/Flags.hpp"
#include "z80/common/RegisterFile.hpp"
#include "z80/common/Scheduler.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <print>
#include <vector>
#endif

namespace specbolt {

export enum class Variant { Spectrum48, Spectrum128 };

export template<typename Z80Impl>
class Spectrum {
public:
  explicit Spectrum(const Variant variant, const std::filesystem::path &rom, const std::size_t audio_sample_rate,
      const double speed_up = 1.0) :
      memory_(total_pages_for(variant)), video_(memory_),
      audio_(audio_sample_rate, static_cast<std::size_t>(50.0 * static_cast<double>(cycles_per_frame) * speed_up)),
      z80_(scheduler_, memory_), variant_(variant) {
    const auto file_size = std::filesystem::file_size(rom);
    const auto SpectrumRomSize = static_cast<std::uint16_t>(0x4000 * rom_pages_for(variant));
    if (file_size != SpectrumRomSize)
      throw std::runtime_error("Bad size for rom file " + rom.string());
    memory_.load(rom, rom_base_page_for(variant), 0, SpectrumRomSize);
    memory_.set_rom_flags({true, false, false, false});
    memory_.set_page_table(page_table_for(variant));
    z80_.add_out_handler([this](const std::uint16_t port, const std::uint8_t value) {
      if ((port & 0xff) == 0xfe) {
        video_.set_border(value & 0x07);
        audio_.set_output(z80_.cycle_count(), value & 0x10, value & 0x8);
      }
    });
    if (variant == Variant::Spectrum128) {
      video_.set_page(5);
      z80_.add_out_handler([this](const std::uint16_t port, const std::uint8_t value) mutable {
        if (paging_disabled_)
          return;
        if (port == 0x7ffd) {
          memory_.set_page_table({
              static_cast<std::uint8_t>(value & 0x10 ? 9 : 8),
              5,
              2,
              static_cast<std::uint8_t>(value & 0x07),
          });
          paging_disabled_ = value & 0x20;
          video_.set_page(static_cast<std::uint8_t>(value & 0x08 ? 7 : 5));
        }
      });
    }
    z80_.add_in_handler([this](const std::uint16_t port) { return keyboard_.in(port); });
    // Handler for ULA sound...
    z80_.add_in_handler([this](const std::uint16_t port) -> std::optional<std::uint8_t> {
      if (port & 1)
        return std::nullopt;
      maybe_detect_loading();
      const auto ear_bit = tape_.level() ? 0x40 : 0x00;
      return std::make_optional<std::uint8_t>(~(1 << 6) | ear_bit);
    });
    reset();
  }
  static constexpr auto cycles_per_frame = static_cast<std::size_t>(3.5 * 1'000'000 / 50);

  std::size_t run_cycles(const std::size_t cycles, const bool keep_history) {
    const auto initial_cycles = z80_.cycle_count();
    const auto end_cycles = initial_cycles + cycles;
    const bool might_need_tracing = trace_next_instructions_ > 0;
    while (z80_.cycle_count() < end_cycles) {
      if (keep_history) {
        reg_history_[current_reg_history_index_ % RegHistory] = z80_.regs();
        ++current_reg_history_index_;
      }
      if (might_need_tracing && trace_next_instructions_ && !z80_.halted()) [[unlikely]] {
        static constexpr auto UndocMask =
            static_cast<std::uint16_t>(0xff00 | ~(Flags::Flag3() | Flags::Flag5()).to_u8());
        const auto time_taken = z80_.cycle_count() - last_traced_instr_cycle_count_;
        last_traced_instr_cycle_count_ = z80_.cycle_count();
        std::print(std::cout, "{:02} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x} {:04x}\n", time_taken, z80_.pc(),
            z80_.regs().get(RegisterFile::R16::AF) & UndocMask, z80_.regs().get(RegisterFile::R16::BC),
            z80_.regs().get(RegisterFile::R16::DE), z80_.regs().get(RegisterFile::R16::HL), z80_.regs().ix(),
            z80_.regs().iy(), z80_.regs().sp());
        --trace_next_instructions_;
      }
      z80_.execute_one();
    }
    return z80_.cycle_count() - initial_cycles;
  }

  std::size_t run_frame() { return run_cycles(cycles_per_frame, false); }

  [[nodiscard]] auto &z80(this auto &&self) { return self.z80_; }
  [[nodiscard]] auto &video(this auto &self) { return self.video_; }
  [[nodiscard]] auto &memory(this auto &self) { return self.memory_; }
  [[nodiscard]] auto &keyboard(this auto &self) { return self.keyboard_; }
  [[nodiscard]] auto &audio(this auto &self) { return self.audio_; }
  [[nodiscard]] auto &tape(this auto &self) { return self.tape_; }

  // TODO do not like, maybe make tape a Task
  void play() {
    tape_.play();
    if (const auto next_transition = tape_.next_transition())
      scheduler_.schedule(tape_task_, next_transition);
  }
  void stop() { tape_.stop(); }

  void trace_next(const std::size_t instructions) { trace_next_instructions_ = instructions; }

  [[nodiscard]] std::vector<RegisterFile> history() const {
    std::vector<RegisterFile> result;
    const auto num_entries = std::min(RegHistory, current_reg_history_index_);
    result.reserve(num_entries + 1);
    for (auto offset = 0u; offset < num_entries; ++offset) {
      const auto index = (current_reg_history_index_ + offset) % RegHistory;
      result.push_back(reg_history_[index]);
    }
    result.push_back(z80_.regs());
    return result;
  }

  void reset() {
    z80_.regs().pc(0);
    if (variant_ == Variant::Spectrum128) {
      memory_.set_page_table(page_table_for(variant_));
      memory_.set_rom_flags({true, false, false, false});
      video_.set_page(5);
    }
    paging_disabled_ = false;
  }

private:
  Memory memory_;
  Video video_;
  Tape tape_;
  Audio audio_;
  Keyboard keyboard_;
  Scheduler scheduler_;
  Z80Impl z80_;
  std::size_t trace_next_instructions_{};
  std::size_t last_traced_instr_cycle_count_{};
  Variant variant_;

  struct VideoTask final : Scheduler::Task {
    Spectrum &spectrum;
    explicit VideoTask(Spectrum &spectrum) : spectrum(spectrum) { spectrum.scheduler_.schedule(*this, 0); }
    void run(std::size_t) override { spectrum.video_line(); }
  };
  VideoTask video_task_{*this};
  void video_line() {
    if (video_.next_scan_line())
      z80_.interrupt();
    scheduler_.schedule(video_task_, Video::CyclesPerScanLine);
  }

  struct TapeTask final : Scheduler::Task {
    Spectrum &spectrum;
    std::size_t last_time_{};
    explicit TapeTask(Spectrum &spectrum) : spectrum(spectrum) {}
    void run(const std::size_t cycles) override {
      spectrum.tape_.pass_time(cycles - last_time_);
      last_time_ = cycles; // ugh
      spectrum.audio_.set_tape_input(cycles, spectrum.tape_.level());
      if (const auto next_transition = spectrum.tape_.next_transition())
        spectrum.scheduler_.schedule(*this, next_transition);
    }
  };
  TapeTask tape_task_{*this};

  // TODO something nicer
  std::size_t last_detect_{};
  std::uint8_t last_b_read_{};
  std::size_t reads_in_a_row_{};
  void maybe_detect_loading() {
    // With thanks to fuse for this heuristic
    const auto since_last = z80_.cycle_count() - last_detect_;
    const auto b_diff = static_cast<std::uint8_t>(z80_.regs().get(RegisterFile::R8::B) - last_b_read_);
    last_detect_ = z80_.cycle_count();
    last_b_read_ = z80_.regs().get(RegisterFile::R8::B);
    if (tape_.playing()) {
      if (since_last > 1000 || (b_diff != 1 && b_diff != 0 && b_diff != 0xff)) {
        if (++reads_in_a_row_ >= 2)
          stop();
      }
      else {
        reads_in_a_row_ = 0;
      }
    }
    else {
      if (since_last <= 500 && (b_diff == 1 || b_diff == 0xff)) {
        if (++reads_in_a_row_ >= 10)
          play();
      }
      else {
        reads_in_a_row_ = 0;
      }
    }
  }
  // END TODO

  static constexpr std::size_t RegHistory = 8uz;
  std::array<RegisterFile, RegHistory> reg_history_{};
  std::size_t current_reg_history_index_{};
  bool paging_disabled_{};

  static constexpr auto total_pages_for(const Variant variant) {
    switch (variant) {
      case Variant::Spectrum48: return 3 + rom_pages_for(variant);
      case Variant::Spectrum128: return 8 + rom_pages_for(variant);
    }
  }
  static constexpr auto rom_pages_for(const Variant variant) {
    switch (variant) {
      case Variant::Spectrum48: return 1;
      case Variant::Spectrum128: return 2;
    }
  }
  static constexpr std::uint8_t rom_base_page_for(const Variant variant) {
    switch (variant) {
      case Variant::Spectrum48: return 0;
      case Variant::Spectrum128: return 8;
    }
  }
  static constexpr std::array<std::uint8_t, 4> page_table_for(const Variant variant) {
    switch (variant) {
      case Variant::Spectrum48: return {0, 1, 2, 3};
      case Variant::Spectrum128: return {8, 5, 2, 0};
    }
  }
};

} // namespace specbolt
