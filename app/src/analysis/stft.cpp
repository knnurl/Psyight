#include "analysis/stft.h"

#include <pffft/pffft.h>

#include <cmath>
#include <numeric>

namespace psyight {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool is_valid_window_size(int n) { return n >= 32 && (n & (n - 1)) == 0; }

struct AlignedFree {
    void operator()(float *p) const { pffft_aligned_free(p); }
};
using AlignedBuffer = std::unique_ptr<float, AlignedFree>;

AlignedBuffer aligned_floats(int count) {
    return AlignedBuffer(static_cast<float *>(pffft_aligned_malloc(count * sizeof(float))));
}

} // namespace

struct Stft::Fft {
    PFFFT_Setup *setup = nullptr;
    AlignedBuffer input;
    AlignedBuffer output;
    AlignedBuffer work;
    ~Fft() {
        if (setup != nullptr) {
            pffft_destroy_setup(setup);
        }
    }
};

Stft::Stft() = default;
Stft::~Stft() = default;

Result<void> Stft::prepare(int sample_rate_hz, int max_block) {
    if (!is_valid_window_size(max_block)) {
        return Error{"STFT window size must be a power of two of at least 32"};
    }
    sample_rate_hz_ = sample_rate_hz;
    window_size_ = max_block;
    fft_ = std::make_unique<Fft>();
    fft_->setup = pffft_new_setup(window_size_, PFFFT_REAL);
    fft_->input = aligned_floats(window_size_);
    fft_->output = aligned_floats(window_size_);
    fft_->work = aligned_floats(window_size_);
    window_.resize(window_size_);
    for (int i = 0; i < window_size_; ++i) {
        window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / window_size_));
    }
    // A sine of amplitude A at a bin centre gives |X| = A * sum(w) / 2.
    const double window_sum = std::accumulate(window_.begin(), window_.end(), 0.0);
    magnitude_scale_ = static_cast<float>(2.0 / window_sum);
    return {};
}

void Stft::process(std::span<const float> signal, SampleIndex centre, std::span<float> magnitudes) {
    float *input = fft_->input.get();
    const SampleIndex start = centre - window_size_ / 2;
    const auto length = static_cast<SampleIndex>(signal.size());
    for (int i = 0; i < window_size_; ++i) {
        const SampleIndex n = start + i;
        input[i] = (n >= 0 && n < length) ? signal[static_cast<size_t>(n)] * window_[i] : 0.0f;
    }
    pffft_transform_ordered(fft_->setup, input, fft_->output.get(), fft_->work.get(),
                            PFFFT_FORWARD);
    // Ordered real output: DC, Nyquist, then (re, im) for bins 1 to N/2 - 1.
    const float *out = fft_->output.get();
    magnitudes[0] = std::abs(out[0]) * magnitude_scale_ * 0.5f;
    magnitudes[window_size_ / 2] = std::abs(out[1]) * magnitude_scale_ * 0.5f;
    for (int bin = 1; bin < window_size_ / 2; ++bin) {
        magnitudes[bin] = std::hypot(out[2 * bin], out[2 * bin + 1]) * magnitude_scale_;
    }
}

} // namespace psyight
