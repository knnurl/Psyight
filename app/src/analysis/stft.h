// Short-time Fourier transform magnitudes (SPEC section 6.1).
//
// Assumptions: mono float input; window size a power of two of at least 32 (pffft's real-FFT
// constraint); periodic Hann window. Frames are centred (file mode): the frame for sample c spans
// [c - N/2, c + N/2), with zeros outside the signal. Magnitudes are scaled so that a full-scale
// sine at a bin centre reads 1.0.
// Latency: non-causal, N/2 samples of lookahead (file mode only).
#pragma once

#include "core/result.h"
#include "core/timebase.h"

#include <memory>
#include <span>
#include <vector>

namespace psyight {

class Stft {
  public:
    Stft();
    ~Stft();
    Stft(const Stft &) = delete;
    Stft &operator=(const Stft &) = delete;

    // max_block is the window size N.
    Result<void> prepare(int sample_rate_hz, int max_block);
    void reset() {} // Stateless between frames.

    // Writes bins() magnitudes for the frame centred on `centre`.
    void process(std::span<const float> signal, SampleIndex centre, std::span<float> magnitudes);

    int window_size() const { return window_size_; }
    int bins() const { return window_size_ / 2 + 1; }
    double bin_hz(int bin) const {
        return static_cast<double>(bin) * sample_rate_hz_ / window_size_;
    }

  private:
    struct Fft;
    std::unique_ptr<Fft> fft_;
    int sample_rate_hz_ = 0;
    int window_size_ = 0;
    std::vector<float> window_;
    float magnitude_scale_ = 1.0f;
};

} // namespace psyight
