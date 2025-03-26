module;

#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

export module peripherals:Blip_Buffer;

// Band-limited sound synthesis and buffering

// Blip_Buffer 0.4.0. http://www.slack.net/~ant/

/* Copyright (C) 2003-2006 Shay Green. This module is free software; you
can redistribute it and/or modify it under the terms of the GNU Lesser
General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version. This
module is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for
more details. You should have received a copy of the GNU Lesser General
Public License along with this module; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA */

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

export class Blip_Buffer {
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
export constexpr int blip_med_quality = 8;
export constexpr int blip_good_quality = 12;
export constexpr int blip_high_quality = 16;

// Range specifies the greatest expected change in amplitude. Calculate it
// by finding the difference between the maximum and minimum expected
// amplitudes (max - min).
export template<int quality, int range>
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
  blip_eq_t(const double treble, const long rolloff_freq, const long sample_rate, const long cutoff_freq = 0) :
      treble(treble), rolloff_freq(rolloff_freq), sample_rate(sample_rate), cutoff_freq(cutoff_freq) {}

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
export constexpr long blip_unscaled = 65535;
export constexpr int blip_low_quality = blip_med_quality;
export constexpr int blip_best_quality = blip_high_quality;

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

static constexpr int buffer_extra = blip_widest_impulse_ + 2;

static constexpr int blip_max_length = 0;

Blip_Buffer::Blip_Buffer() {
  factor_ = LONG_MAX;
  offset_ = 0;
  sample_rate_ = 0;
  reader_accum = 0;
  bass_shift = 0;
  clock_rate_ = 0;
  bass_freq_ = 16;
  length_ = 0;

// assumptions code makes about implementation-defined features
#ifndef NDEBUG
  // right shift of negative value preserves sign
  constexpr int i = INT_MIN;
  assert((i >> 1) == INT_MIN / 2);

  // casting to smaller signed type truncates bits and extends sign
  constexpr long l = (SHRT_MAX + 1) * 5;
  assert(static_cast<short>(l) == SHRT_MIN);
#endif
}
std::size_t Blip_Buffer::buffer_size() const {
  if (buffer_.size() < buffer_extra)
    return 0;
  return buffer_.size() - buffer_extra;
}

void Blip_Buffer::clear(const bool entire_buffer) {
  offset_ = 0;
  reader_accum = 0;
  const auto count = (entire_buffer ? buffer_size() : samples_avail());
  memset(buffer_.data(), 0, (count + buffer_extra) * sizeof(buf_t_));
}

void Blip_Buffer::set_sample_rate(const unsigned long new_rate, const unsigned int msec) {
  // start with maximum length that resampled time can represent
  std::size_t new_size = (ULONG_MAX >> BLIP_BUFFER_ACCURACY) - buffer_extra - 64zu;
  if (msec != blip_max_length) {
    const std::size_t s = (new_rate * (msec + 1zu) + 999zu) / 1000zu;
    if (s < new_size)
      new_size = s;
    else
      throw std::runtime_error("requested buffer length exceeds limit");
  }

  buffer_.resize(new_size + buffer_extra);

  // update things based on the sample rate
  sample_rate_ = new_rate;
  length_ = new_size * 1000zu / new_rate - 1;
  if (msec)
    assert(length_ == msec); // ensure length is same as that passed in
  if (clock_rate_)
    clock_rate(clock_rate_);
  bass_freq(bass_freq_);

  clear();
}

blip_resampled_time_t Blip_Buffer::clock_rate_factor(const std::size_t clock_rate) const {
  const double ratio = static_cast<double>(sample_rate_) / static_cast<double>(clock_rate);
  const long factor = static_cast<long>(floor(ratio * (1L << BLIP_BUFFER_ACCURACY) + 0.5));
  assert(factor > 0 || !sample_rate_); // fails if clock/output ratio is too large
  return static_cast<blip_resampled_time_t>(factor);
}

void Blip_Buffer::bass_freq(const int freq) {
  bass_freq_ = freq;
  int shift = 31;
  if (freq > 0) {
    shift = 13;
    auto f = (static_cast<std::size_t>(freq) << 16) / sample_rate_;
    while ((f >>= 1) && --shift) {
    }
  }
  bass_shift = shift;
}

void Blip_Buffer::end_frame(const blip_time_t time) {
  offset_ += time * factor_;
  assert(samples_avail() <= buffer_size()); // time outside buffer length
}

void Blip_Buffer::remove_silence(const unsigned long count) {
  assert(count <= samples_avail()); // tried to remove more samples than available
  offset_ -= static_cast<blip_resampled_time_t>(count) << BLIP_BUFFER_ACCURACY;
}

long Blip_Buffer::count_samples(const blip_time_t t) const {
  const unsigned long last_sample = resampled_time(t) >> BLIP_BUFFER_ACCURACY;
  const unsigned long first_sample = offset_ >> BLIP_BUFFER_ACCURACY;
  return static_cast<long>(last_sample - first_sample);
}

blip_time_t Blip_Buffer::count_clocks(std::size_t count) const {
  if (count > buffer_size())
    count = buffer_size();
  const blip_resampled_time_t time = static_cast<blip_resampled_time_t>(count) << BLIP_BUFFER_ACCURACY;
  return static_cast<blip_time_t>((time - offset_ + factor_ - 1) / factor_);
}

void Blip_Buffer::remove_samples(const unsigned long count) {
  if (count) {
    remove_silence(count);

    // copy remaining samples to beginning and clear old samples
    const auto remain = samples_avail() + buffer_extra;
    memmove(buffer_.data(), buffer_.data() + count, remain * sizeof(buf_t_));
    memset(buffer_.data() + remain, 0, count * sizeof(buf_t_));
  }
}

// Blip_Synth_

Blip_Synth_::Blip_Synth_(short *impulses, const int width) : impulses(impulses), width(width) {
  volume_unit_ = 0.0;
  kernel_unit = 0;
  buf = nullptr;
  last_amp = 0;
  delta_factor = 0;
}

static constexpr double pi = 3.1415926535897932384626433832795029;

static void gen_sinc(float *out, const int count, const double oversample, double treble, double cutoff) {
  if (cutoff >= 0.999)
    cutoff = 0.999;

  if (treble < -300.0)
    treble = -300.0;
  if (treble > 5.0)
    treble = 5.0;

  constexpr double maxh = 4096.0;
  const double rolloff = pow(10.0, 1.0 / (maxh * 20.0) * treble / (1.0 - cutoff));
  const double pow_a_n = pow(rolloff, maxh - maxh * cutoff);
  const double to_angle = pi / 2 / maxh / oversample;
  for (int i = 0; i < count; i++) {
    const double angle = ((i - count) * 2 + 1) * to_angle;
    double c = rolloff * cos((maxh - 1.0) * angle) - cos(maxh * angle);
    const double cos_nc_angle = cos(maxh * cutoff * angle);
    const double cos_nc1_angle = cos((maxh * cutoff - 1.0) * angle);
    const double cos_angle = cos(angle);

    c = c * pow_a_n - rolloff * cos_nc1_angle + cos_nc_angle;
    const double d = 1.0 + rolloff * (rolloff - cos_angle - cos_angle);
    const double b = 2.0 - cos_angle - cos_angle;
    const double a = 1.0 - cos_angle - cos_nc_angle + cos_nc1_angle;

    out[i] = static_cast<float>((a * d + c * b) / (b * d)); // a / b + c / d
  }
}

void blip_eq_t::generate(float *out, const int count) const {
  // lower cutoff freq for narrow kernels with their wider transition band
  // (8 points->1.49, 16 points->1.15)
  double oversample = blip_res * 2.25 / count + 0.85;
  const double half_rate = static_cast<double>(sample_rate) * 0.5;
  if (cutoff_freq)
    oversample = half_rate / static_cast<double>(cutoff_freq);
  const double cutoff = static_cast<double>(rolloff_freq) * oversample / half_rate;

  gen_sinc(out, count, blip_res * oversample, treble, cutoff);

  // apply (half of) hamming window
  const double to_fraction = pi / (count - 1);
  for (int i = count; i--;)
    out[i] *= static_cast<float>(0.54 - 0.46 * cos(i * to_fraction));
}

void Blip_Synth_::adjust_impulse() {
  // sum pairs for each phase and add error correction to end of first half
  const int size = impulses_size();
  for (int p = blip_res; p-- >= blip_res / 2;) {
    const int p2 = blip_res - 2 - p;
    long error = kernel_unit;
    for (int i = 1; i < size; i += blip_res) {
      error -= impulses[i + p];
      error -= impulses[i + p2];
    }
    if (p == p2)
      error /= 2; // phase = 0.5 impulse uses same half for both sides
    impulses[size - blip_res + p] += static_cast<short>(error);
    // printf( "error: %ld\n", error );
  }

  // for ( int i = blip_res; i--; printf( "\n" ) )
  //   for ( int j = 0; j < width / 2; j++ )
  //       printf( "%5ld,", impulses [j * blip_res + i + 1] );
}

void Blip_Synth_::treble_eq(const blip_eq_t &eq) {
  float fimpulse[blip_res / 2 * (blip_widest_impulse_ - 1) + blip_res * 2];

  const int half_size = blip_res / 2 * (width - 1);
  eq.generate(&fimpulse[blip_res], half_size);

  int i;

  // need mirror slightly past center for calculation
  for (i = blip_res; i--;)
    fimpulse[blip_res + half_size + i] = fimpulse[blip_res + half_size - 1 - i];

  // starts at 0
  for (i = 0; i < blip_res; i++)
    fimpulse[i] = 0.0f;

  // find rescale factor
  double total = 0.0;
  for (i = 0; i < half_size; i++)
    total += fimpulse[blip_res + i];

  // double const base_unit = 44800.0 - 128 * 18; // allows treble up to +0 dB
  // double const base_unit = 37888.0; // allows treble to +5 dB
  constexpr double base_unit = 32768.0; // necessary for blip_unscaled to work
  const double rescale = base_unit / 2 / total;
  kernel_unit = static_cast<long>(base_unit);

  // integrate, first difference, rescale, convert to int
  double sum = 0.0;
  double next = 0.0;
  const int impulses_size = this->impulses_size();
  for (i = 0; i < impulses_size; i++) {
    impulses[i] = static_cast<short>(floor((next - sum) * rescale + 0.5));
    sum += fimpulse[i];
    next += fimpulse[i + blip_res];
  }
  adjust_impulse();

  // volume might require rescaling
  const double vol = volume_unit_;
  if (vol != 0.0) {
    volume_unit_ = 0.0;
    volume_unit(vol);
  }
}

void Blip_Synth_::volume_unit(const double new_unit) {
  if (new_unit != volume_unit_) {
    // use default eq if it hasn't been set yet
    if (!kernel_unit)
      treble_eq(-8.0);

    volume_unit_ = new_unit;
    double factor = new_unit * (1L << blip_sample_bits) / static_cast<double>(kernel_unit);

    if (factor > 0.0) {
      int shift = 0;

      // if unit is really small, might need to attenuate kernel
      while (factor < 2.0) {
        shift++;
        factor *= 2.0;
      }

      if (shift) {
        kernel_unit >>= shift;
        assert(kernel_unit > 0); // fails if volume unit is too low

        // keep values positive to avoid round-towards-zero of sign-preserving
        // right shift for negative values
        const long offset = 0x8000 + (1 << (shift - 1));
        const long offset2 = 0x8000 >> shift;
        for (int i = impulses_size(); i--;)
          impulses[i] = static_cast<short>(((impulses[i] + offset) >> shift) - offset2);
        adjust_impulse();
      }
    }
    delta_factor = static_cast<int>(floor(factor + 0.5));
    // printf( "delta_factor: %d, kernel_unit: %d\n", delta_factor, kernel_unit );
  }
}

std::size_t Blip_Buffer::read_samples(blip_sample_t *out, const std::size_t max_samples, const bool stereo) {
  auto count = samples_avail();
  if (count > max_samples)
    count = max_samples;

  if (count) {
    constexpr int sample_shift = blip_sample_bits - 16;
    long accum = reader_accum;
    const buf_t_ *in = buffer_.data();

    if (!stereo) {
      for (auto n = count; n--;) {
        const auto s = accum >> sample_shift;
        accum -= accum >> bass_shift;
        accum += *in++;
        *out++ = static_cast<blip_sample_t>(s);

        // clamp sample
        if (static_cast<blip_sample_t>(s) != s)
          out[-1] = static_cast<blip_sample_t>(0x7FFF - (s >> 24));
      }
    }
    else {
      for (auto n = count; n--;) {
        const auto s = accum >> sample_shift;
        accum -= accum >> bass_shift;
        accum += *in++;
        *out = static_cast<blip_sample_t>(s);
        out += 2;

        // clamp sample
        if (static_cast<blip_sample_t>(s) != s)
          out[-2] = static_cast<blip_sample_t>(0x7FFF - (s >> 24));
      }
    }

    reader_accum = accum;
    remove_samples(count);
  }
  return count;
}

void Blip_Buffer::mix_samples(const blip_sample_t *in, long count) {
  buf_t_ *out = buffer_.data() + (offset_ >> BLIP_BUFFER_ACCURACY) + blip_widest_impulse_ / 2;

  constexpr int sample_shift = blip_sample_bits - 16;
  long prev = 0;
  while (count--) {
    const long s = static_cast<long>(*in++) << sample_shift;
    *out += s - prev;
    prev = s;
    ++out;
  }
  *out -= prev;
}
