
// Blip_Buffer 0.4.0. http://www.slack.net/~ant/

#ifndef SPECBOLT_MODULES
#include "peripherals/Blip_Buffer.hpp"

#include <cassert>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#endif

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

static constexpr int buffer_extra = blip_widest_impulse_ + 2;

static constexpr int blip_max_length = 0;

Blip_Buffer::Blip_Buffer() {
  factor_ = std::numeric_limits<long>::max();
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
  constexpr int i = std::numeric_limits<int>::min();
  assert((i >> 1) == std::numeric_limits<int>::min() / 2);

  // casting to smaller signed type truncates bits and extends sign
  constexpr long l = (std::numeric_limits<short>::max() + 1) * 5;
  assert(static_cast<short>(l) == std::numeric_limits<short>::min());
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
  std::memset(buffer_.data(), 0, (count + buffer_extra) * sizeof(buf_t_));
}

void Blip_Buffer::set_sample_rate(const unsigned long new_rate, const unsigned int msec) {
  // start with maximum length that resampled time can represent
  std::size_t new_size = (std::numeric_limits<unsigned long>::max() >> BLIP_BUFFER_ACCURACY) - buffer_extra - 64zu;
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
  const long factor = static_cast<long>(std::floor(ratio * (1L << BLIP_BUFFER_ACCURACY) + 0.5));
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
    std::memmove(buffer_.data(), buffer_.data() + count, remain * sizeof(buf_t_));
    std::memset(buffer_.data() + remain, 0, count * sizeof(buf_t_));
  }
}

// Blip_Synth_

Blip_Synth_::Blip_Synth_(short *impulses_, const int width_) : impulses(impulses_), width(width_) {
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
  const double rolloff = std::pow(10.0, 1.0 / (maxh * 20.0) * treble / (1.0 - cutoff));
  const double pow_a_n = std::pow(rolloff, maxh - maxh * cutoff);
  const double to_angle = pi / 2 / maxh / oversample;
  for (int i = 0; i < count; i++) {
    const double angle = ((i - count) * 2 + 1) * to_angle;
    double c = rolloff * std::cos((maxh - 1.0) * angle) - std::cos(maxh * angle);
    const double cos_nc_angle = std::cos(maxh * cutoff * angle);
    const double cos_nc1_angle = std::cos((maxh * cutoff - 1.0) * angle);
    const double cos_angle = std::cos(angle);

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
    out[i] *= static_cast<float>(0.54 - 0.46 * std::cos(i * to_fraction));
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
    impulses[i] = static_cast<short>(std::floor((next - sum) * rescale + 0.5));
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
    delta_factor = static_cast<int>(std::floor(factor + 0.5));
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
