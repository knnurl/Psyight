// Alignment of a stem sum against its mix.

#include "analysis/align.h"
#include "synth_generator.h"

#include <doctest/doctest.h>

#include <vector>

using namespace psyight;

TEST_CASE("align: the lag of a shifted, partial copy is found to the sample") {
    synth::SynthParams params;
    params.bars = 8;
    params.hats = true;
    const synth::SynthOutput mix =
        synth::render(params, synth::make_pattern(synth::Pattern::rolling, 8, 29, 0.6));
    synth::SynthParams bass_only = params;
    bass_only.kick = false;
    bass_only.hats = false;
    const synth::SynthOutput bass =
        synth::render(bass_only, synth::make_pattern(synth::Pattern::rolling, 8, 29, 0.6));
    for (const std::int64_t lag : {0, 1, 17, 2045, -731}) {
        CAPTURE(lag);
        // The "stem" is the bass alone, delayed by lag samples (padded or trimmed at the start).
        std::vector<float> stem(bass.samples.size(), 0.0f);
        for (size_t i = 0; i < stem.size(); ++i) {
            const auto source = static_cast<std::int64_t>(i) - lag;
            if (source >= 0 && source < static_cast<std::int64_t>(bass.samples.size())) {
                stem[i] = bass.samples[static_cast<size_t>(source)];
            }
        }
        CHECK(estimate_lag_samples(mix.samples, stem, 4800) == lag);
    }
}

TEST_CASE("align: too little audio gives no lag rather than a guess") {
    const std::vector<float> short_audio(100, 0.5f);
    CHECK(estimate_lag_samples(short_audio, short_audio, 4800) == 0);
}
