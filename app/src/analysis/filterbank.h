// IIR band-pass filterbank with envelope followers (SPEC section 6.1): low-latency band timing,
// where short FFT windows cannot resolve the low end.
//
// BandFilter: two cascaded RBJ constant-0-dB-peak band-pass biquads (4th order) centred on the
// geometric mean of the band edges, unity gain at the centre.
// Assumptions: edges satisfy 0 < low < high < sample_rate / 2. Double-precision state.
// Latency: causal; group delay of a few periods of the centre frequency (largest for sub bands).
//
// EnvelopeFollower: rectified one-pole follower with separate attack and release.
// Latency: causal; the attack time constant.
#pragma once

#include <span>

namespace psyight {

class Biquad {
  public:
    // RBJ cookbook band-pass, constant 0 dB peak gain.
    void set_band_pass(double centre_hz, double q, double sample_rate_hz);
    void reset() { z1_ = z2_ = 0.0; }
    double process(double x) {
        const double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }

  private:
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0; // Transposed direct form II.
};

class BandFilter {
  public:
    void prepare(double sample_rate_hz, double low_hz, double high_hz);
    void reset();
    void process(std::span<const float> in, std::span<float> out);

  private:
    Biquad stages_[2];
};

class EnvelopeFollower {
  public:
    void prepare(double sample_rate_hz, double attack_ms, double release_ms);
    void reset() { level_ = 0.0; }
    // Follows |x|; returns the envelope after this sample.
    double process(double x);

  private:
    double attack_coeff_ = 0.0;
    double release_coeff_ = 0.0;
    double level_ = 0.0;
};

} // namespace psyight
