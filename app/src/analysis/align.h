// Time alignment of two recordings of the same audio, for example Demucs stems against the mix:
// the stems are decoded by another MP3 decoder, whose encoder-delay handling can shift them by
// tens of milliseconds (OculusDrift: 42.6 ms).
//
// The lag maximising the cross-correlation of `signal` against `reference`, searched within
// +-max_lag_samples: coarse on 16x decimated copies (box-filtered), then sample-exact around the
// coarse peak. Uses up to window_samples taken from the middle of the reference.
// Assumptions: both at the same sample rate; the audio is correlated (a stem sum against its mix).
// Deterministic. Not real-time.
#pragma once

#include <cstdint>
#include <span>

namespace psyight {

// Positive: `signal` lags `reference` (signal[i + lag] lines up with reference[i]).
std::int64_t estimate_lag_samples(std::span<const float> reference, std::span<const float> signal,
                                  std::int64_t max_lag_samples,
                                  std::int64_t window_samples = 30 * 48000);

} // namespace psyight
