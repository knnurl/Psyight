#include "analysis/filterbank.h"

#include <cmath>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;

// One-pole coefficient reaching 1 - 1/e of a step in time_ms.
double one_pole_coeff(double time_ms, double sample_rate_hz) {
    return time_ms <= 0.0 ? 0.0 : std::exp(-1.0 / (time_ms * 1e-3 * sample_rate_hz));
}

} // namespace

void Biquad::set_band_pass(double centre_hz, double q, double sample_rate_hz) {
    const double w0 = 2.0 * kPi * centre_hz / sample_rate_hz;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0_ = alpha / a0;
    b1_ = 0.0;
    b2_ = -alpha / a0;
    a1_ = -2.0 * std::cos(w0) / a0;
    a2_ = (1.0 - alpha) / a0;
}

void BandFilter::prepare(double sample_rate_hz, double low_hz, double high_hz) {
    const double centre_hz = std::sqrt(low_hz * high_hz);
    // Each stage gets the band's Q; the cascade narrows the -3 dB width to about 0.64 of the band,
    // in exchange for steeper skirts (12 dB per octave far from the centre, against 6 for one).
    const double q = centre_hz / (high_hz - low_hz);
    for (Biquad &stage : stages_) {
        stage.set_band_pass(centre_hz, q, sample_rate_hz);
    }
    reset();
}

void BandFilter::reset() {
    for (Biquad &stage : stages_) {
        stage.reset();
    }
}

void BandFilter::process(std::span<const float> in, std::span<float> out) {
    for (size_t i = 0; i < in.size(); ++i) {
        out[i] = static_cast<float>(stages_[1].process(stages_[0].process(in[i])));
    }
}

void EnvelopeFollower::prepare(double sample_rate_hz, double attack_ms, double release_ms) {
    attack_coeff_ = one_pole_coeff(attack_ms, sample_rate_hz);
    release_coeff_ = one_pole_coeff(release_ms, sample_rate_hz);
    reset();
}

double EnvelopeFollower::process(double x) {
    const double rectified = std::abs(x);
    const double coeff = rectified > level_ ? attack_coeff_ : release_coeff_;
    level_ = rectified + coeff * (level_ - rectified);
    return level_;
}

} // namespace psyight
