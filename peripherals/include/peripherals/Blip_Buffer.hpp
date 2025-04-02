// Band-limited sound synthesis and buffering

// Blip_Buffer 0.4.0

#pragma once

#ifndef SPECBOLT_MODULES
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>
#endif

// Time unit at source clock rate
using blip_time_t = std::size_t;

// Output samples are 16-bit signed, with a range of -32767 to 32767
using blip_sample_t = std::int16_t;
enum { blip_sample_max = 32767 };

// Number of bits in resample ratio fraction. Higher values give a more accurate ratio
// but reduce maximum buffer size.
constexpr unsigned BLIP_BUFFER_ACCURACY = 16;

// Number bits in phase offset. Fewer than 6 bits (64 phase offsets) results in
// noticeable broadband noise when synthesizing high frequency square waves.
// Affects size of Blip_Synth objects since they store the waveform directly.
constexpr unsigned BLIP_PHASE_BITS = 6;

// Internal
using blip_resampled_time_t = unsigned long;
constexpr int blip_widest_impulse_ = 16;
constexpr int blip_res = 1 << BLIP_PHASE_BITS;
class blip_eq_t;

SPECBOLT_EXPORT
class Blip_Buffer {
public:
  // Set output sample rate and buffer length in milliseconds (1/1000 sec, defaults
  // to 1/4 second), then clear buffer.
  void set_sample_rate(unsigned long new_rate, unsigned int msec = 1000 / 4);

  // Set number of source time units per second
  void clock_rate(const std::size_t rate) { factor_ = clock_rate_factor(clock_rate_ = rate); }

  // End current time frame of specified duration and make its samples available
  // (along with any still-unread samples) for reading with read_samples(). Begins
  // a new time frame at the end of the current frame.
  void end_frame(blip_time_t time);

  // Read at most 'max_samples' out of buffer into 'dest', removing them from from
  // the buffer. Returns number of samples actually read and removed. If stereo is
  // true, increments 'dest' one extra time after writing each sample, to allow
  // easy interleving of two channels into a stereo output buffer.
  std::size_t read_samples(blip_sample_t *dest, std::size_t max_samples, bool stereo = false);

  // Additional optional features

  // Current output sample rate
  [[nodiscard]] std::size_t sample_rate() const { return sample_rate_; }

  // Length of buffer, in milliseconds
  [[nodiscard]] std::size_t length() const { return length_; }

  // Number of source time units per second
  [[nodiscard]] std::size_t clock_rate() const { return clock_rate_; }

  // Set frequency high-pass filter frequency, where higher values reduce bass more
  void bass_freq(int frequency);

  // Number of samples delay from synthesis to samples read out
  [[nodiscard]] static int output_latency() { return blip_widest_impulse_ / 2; }

  // Remove all available samples and clear buffer to silence. If 'entire_buffer' is
  // false, just clears out any samples waiting rather than the entire buffer.
  void clear(bool entire_buffer = true);

  // Number of samples available for reading with read_samples()
  [[nodiscard]] std::size_t samples_avail() const { return (offset_ >> BLIP_BUFFER_ACCURACY); }

  // Remove 'count' samples from those waiting to be read
  void remove_samples(unsigned long count);

  // Experimental features

  // Number of raw samples that can be mixed within frame of specified duration.
  [[nodiscard]] long count_samples(blip_time_t duration) const;

  // Mix 'count' samples from 'buf' into buffer.
  void mix_samples(const blip_sample_t *buf, long count);

  // Count number of clocks needed until 'count' samples will be available.
  // If buffer can't even hold 'count' samples, returns number of clocks until
  // buffer becomes full.
  [[nodiscard]] blip_time_t count_clocks(std::size_t count) const;

  // not documented yet
  using blip_resampled_time_t = unsigned long;
  void remove_silence(unsigned long count);
  [[nodiscard]] blip_resampled_time_t resampled_duration(const unsigned int t) const { return t * factor_; }
  [[nodiscard]] blip_resampled_time_t resampled_time(const blip_time_t t) const {
    return static_cast<blip_resampled_time_t>(t) * factor_ + offset_;
  }
  [[nodiscard]] blip_resampled_time_t clock_rate_factor(std::size_t clock_rate) const;

  Blip_Buffer();

  // Deprecated
  using resampled_time_t = blip_resampled_time_t;
  void sample_rate(const unsigned long r) { set_sample_rate(r); }
  void sample_rate(const unsigned long r, const unsigned int msec) { set_sample_rate(r, msec); }

  // noncopyable
  Blip_Buffer(const Blip_Buffer &) = delete;
  Blip_Buffer &operator=(const Blip_Buffer &) = delete;

  using buf_t_ = long;
  [[nodiscard]] std::size_t buffer_size() const;
  buf_t_ *buffer_data() { return buffer_.data(); }

  [[nodiscard]] auto offset() const { return offset_; }
  [[nodiscard]] auto factor() const { return factor_; }

private:
  unsigned long factor_;
  blip_resampled_time_t offset_;
  std::vector<buf_t_> buffer_;

  long reader_accum;
  int bass_shift;
  unsigned long sample_rate_;
  std::size_t clock_rate_;
  int bass_freq_;
  std::size_t length_;
  friend class Blip_Reader;
};

class Blip_Synth_ {
  double volume_unit_;
  short *const impulses;
  const int width;
  long kernel_unit;
  [[nodiscard]] auto impulses_size() const { return blip_res / 2 * width + 1; }
  void adjust_impulse();

public:
  Blip_Buffer *buf;
  int last_amp;
  int delta_factor;

  Blip_Synth_(short *impulses, int width);
  void treble_eq(const blip_eq_t &);
  void volume_unit(double);
};

// Quality level. Start with blip_good_quality.
SPECBOLT_EXPORT constexpr int blip_med_quality = 8;
SPECBOLT_EXPORT constexpr int blip_good_quality = 12;
SPECBOLT_EXPORT constexpr int blip_high_quality = 16;

// Range specifies the greatest expected change in amplitude. Calculate it
// by finding the difference between the maximum and minimum expected
// amplitudes (max - min).
SPECBOLT_EXPORT
template<int quality, int range>
class Blip_Synth {
public:
  // Set overall volume of waveform
  void volume(const double v) { impl.volume_unit(v * (1.0 / (range < 0 ? -range : range))); }

  // Configure low-pass filter (see notes.txt)
  void treble_eq(const blip_eq_t &eq) { impl.treble_eq(eq); }

  // Get/set Blip_Buffer used for output
  [[nodiscard]] Blip_Buffer *output() const { return impl.buf; }
  void output(Blip_Buffer *b) {
    impl.buf = b;
    impl.last_amp = 0;
  }

  // Update amplitude of waveform at given time. Using this requires a separate
  // Blip_Synth for each waveform.
  void update(blip_time_t time, int amplitude);

  // Low-level interface

  // Add an amplitude transition of specified delta, optionally into specified buffer
  // rather than the one set with output(). Delta can be positive or negative.
  // The actual change in amplitude is delta * (volume / range)
  void offset(blip_time_t, int delta, Blip_Buffer *) const;
  void offset(const blip_time_t t, const int delta) const { offset(t, delta, impl.buf); }

  // Works directly in terms of fractional output samples. Contact author for more.
  void offset_resampled(blip_resampled_time_t, int delta, Blip_Buffer *) const;

  // Same as offset(), except code is inlined for higher performance
  void offset_inline(const blip_time_t t, const int delta, Blip_Buffer *buf) const {
    offset_resampled(t * buf->factor() + buf->offset(), delta, buf);
  }
  void offset_inline(const blip_time_t t, const int delta) const {
    offset_resampled(t * impl.buf->factor() + impl.buf->offset(), delta, impl.buf);
  }

  Blip_Synth() : impl(impulses.data(), quality) {}

private:
  using imp_t = short;
  static constexpr auto impulse_size = blip_res * (quality / 2) + 1;
  std::array<imp_t, impulse_size> impulses{};
  Blip_Synth_ impl;
};

// Low-pass equalization parameters
class blip_eq_t {
public:
  // Logarithmic rolloff to treble dB at half sampling rate. Negative values reduce
  // treble, small positive values (0 to 5.0) increase treble.
  // ReSharper disable once CppNonExplicitConvertingConstructor
  blip_eq_t(double treble_db = 0) : blip_eq_t(treble_db, 0, 44100, 0) {} // NOLINT(*-explicit-constructor)

  // See notes.txt
  blip_eq_t(const double treble_, const long rolloff_freq_, const long sample_rate_, const long cutoff_freq_ = 0) :
      treble(treble_), rolloff_freq(rolloff_freq_), sample_rate(sample_rate_), cutoff_freq(cutoff_freq_) {}

private:
  double treble;
  long rolloff_freq;
  long sample_rate;
  long cutoff_freq;
  void generate(float *out, int count) const;
  friend class Blip_Synth_;
};

constexpr int blip_sample_bits = 30;

// Optimized inline sample reader for custom sample formats and mixing of Blip_Buffer samples
class Blip_Reader {
public:
  // Begin reading samples from buffer. Returns value to pass to next() (can
  // be ignored if default bass_freq is acceptable).
  int begin(const Blip_Buffer &blip_buf) {
    buf = blip_buf.buffer_.data();
    accum = blip_buf.reader_accum;
    return blip_buf.bass_shift;
  }

  // Current sample
  [[nodiscard]] long read() const { return accum >> (blip_sample_bits - 16); }

  // Current raw sample in full internal resolution
  [[nodiscard]] long read_raw() const { return accum; }

  // Advance to next sample
  void next(const int bass_shift = 9) { accum += *buf++ - (accum >> bass_shift); }

  // End reading samples from buffer. The number of samples read must now be removed
  // using Blip_Buffer::remove_samples().
  void end(Blip_Buffer &b) { b.reader_accum = accum; }

private:
  const Blip_Buffer::buf_t_ *buf{};
  long accum{};
};


// End of public interface


// Compatibility with older version
SPECBOLT_EXPORT constexpr long blip_unscaled = 65535;
SPECBOLT_EXPORT constexpr int blip_low_quality = blip_med_quality;
SPECBOLT_EXPORT constexpr int blip_best_quality = blip_high_quality;

template<int quality, int range>
void Blip_Synth<quality, range>::offset_resampled(
    const blip_resampled_time_t time, int delta, Blip_Buffer *blip_buf) const {
  // Fails if time is beyond end of Blip_Buffer, due to a bug in caller code or the
  // need for a longer buffer as set by set_sample_rate().
  assert((time >> BLIP_BUFFER_ACCURACY) < blip_buf->buffer_size());
  delta *= impl.delta_factor;
  const auto phase = (time >> (BLIP_BUFFER_ACCURACY - BLIP_PHASE_BITS) & (blip_res - 1));
  const imp_t *imp = impulses.data() + blip_res - phase;
  long *buf = blip_buf->buffer_data() + (time >> BLIP_BUFFER_ACCURACY);
  long i0 = *imp;

  const int fwd = (blip_widest_impulse_ - quality) / 2;
  const int rev = fwd + quality - 2;

  const auto BLIP_FWD = [&](const int i) {
    const long t0 = i0 * delta + buf[fwd + i];
    const long t1 = imp[blip_res * (i + 1)] * delta + buf[fwd + 1 + i];
    i0 = imp[blip_res * (i + 2)];
    buf[fwd + i] = t0;
    buf[fwd + 1 + i] = t1;
  };
  const auto BLIP_REV = [&](const int r) {
    long t0 = i0 * delta + buf[rev - r];
    long t1 = imp[blip_res * r] * delta + buf[rev + 1 - r];
    i0 = imp[blip_res * (r - 1)];
    buf[rev - r] = t0;
    buf[rev + 1 - r] = t1;
  };

  BLIP_FWD(0);
  if (quality > 8)
    BLIP_FWD(2);
  if (quality > 12)
    BLIP_FWD(4);
  {
    const int mid = quality / 2 - 1;
    const long t0 = i0 * delta + buf[fwd + mid - 1];
    const long t1 = imp[blip_res * mid] * delta + buf[fwd + mid];
    imp = impulses.data() + phase;
    i0 = imp[blip_res * mid];
    buf[fwd + mid - 1] = t0;
    buf[fwd + mid] = t1;
  }
  if (quality > 12)
    BLIP_REV(6);
  if (quality > 8)
    BLIP_REV(4);
  BLIP_REV(2);

  const long t0 = i0 * delta + buf[rev];
  const long t1 = *imp * delta + buf[rev + 1];
  buf[rev] = t0;
  buf[rev + 1] = t1;
}

template<int quality, int range>
void Blip_Synth<quality, range>::offset(const blip_time_t t, const int delta, Blip_Buffer *buf) const {
  offset_resampled(t * buf->factor() + buf->offset(), delta, buf);
}

template<int quality, int range>
void Blip_Synth<quality, range>::update(const blip_time_t time, const int amplitude) {
  const int delta = amplitude - impl.last_amp;
  impl.last_amp = amplitude;
  offset_resampled(time * impl.buf->factor() + impl.buf->offset(), delta, impl.buf);
}
