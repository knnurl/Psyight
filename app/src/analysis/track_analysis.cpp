#include "analysis/track_analysis.h"

#include "analysis/align.h"
#include "audio/audio_file.h"
#include "core/paths.h"
#include "io/stems.h"

#include <cmath>

namespace psyight {

namespace {

Result<std::vector<float>> decode_mono(const std::filesystem::path &path, int &sample_rate_hz) {
    Result<AudioBuffer> audio = decode_audio_file(path);
    if (!audio) {
        return audio.error();
    }
    sample_rate_hz = audio.value().sample_rate_hz;
    return mono_mix(audio.value());
}

// Stems decoded by another MP3 decoder can sit tens of milliseconds off the app's decoding of the
// mix; this many samples either way are searched.
constexpr std::int64_t kMaxStemLagSamples = 24000;

// x[i + lag] moved to i, zero-filled at the edges.
std::vector<float> shifted(const std::vector<float> &x, std::int64_t lag) {
    std::vector<float> y(x.size(), 0.0f);
    for (size_t i = 0; i < y.size(); ++i) {
        const std::int64_t j = static_cast<std::int64_t>(i) + lag;
        if (j >= 0 && j < static_cast<std::int64_t>(x.size())) {
            y[i] = x[static_cast<size_t>(j)];
        }
    }
    return y;
}

} // namespace

Result<TrackAnalysis> analyse_track(const std::filesystem::path &audio_path,
                                    const TrackAnalysisOptions &options) {
    int sample_rate_hz = 0;
    Result<std::vector<float>> mix = decode_mono(audio_path, sample_rate_hz);
    if (!mix) {
        return mix.error();
    }
    return analyse_track(mix.value(), sample_rate_hz, audio_path, options);
}

Result<TrackAnalysis> analyse_track(std::span<const float> mix, int sample_rate_hz,
                                    const std::filesystem::path &audio_path,
                                    const TrackAnalysisOptions &options) {
    TrackAnalysis analysis;
    analysis.sample_rate_hz = sample_rate_hz;
    const int sr = analysis.sample_rate_hz;
    analysis.duration_s = static_cast<double>(mix.size()) / sr;
    Result<FrontEndResult> mix_features = analyse_front_end(mix, sr, options.front_end);
    if (!mix_features) {
        return mix_features.error();
    }
    analysis.mix_features = std::move(mix_features.value());

    // The beat and kick source: the mix, or the drums stem. Stems are aligned to the mix first.
    std::vector<float> drums;
    std::vector<float> bass_stem;
    FrontEndResult drums_features;
    const StemPaths stems = stem_paths_for(audio_path);
    if (options.source == AnalysisSource::stems) {
        if (!analysis_stems_exist(stems)) {
            return Error{"no stems for " + path_to_utf8(audio_path.filename()) +
                         ": run tools/python/separate.py"};
        }
        int stem_rate = 0;
        Result<std::vector<float>> decoded = decode_mono(stems.drums, stem_rate);
        if (!decoded) {
            return decoded.error();
        }
        drums = std::move(decoded.value());
        decoded = decode_mono(stems.bass, stem_rate);
        if (!decoded) {
            return decoded.error();
        }
        bass_stem = std::move(decoded.value());
        std::vector<float> stem_sum(std::min(drums.size(), bass_stem.size()));
        for (size_t i = 0; i < stem_sum.size(); ++i) {
            stem_sum[i] = drums[i] + bass_stem[i];
        }
        const std::int64_t lag = estimate_lag_samples(mix, stem_sum, kMaxStemLagSamples);
        analysis.stems_lag_s = static_cast<double>(lag) / stem_rate;
        drums = shifted(drums, lag);
        bass_stem = shifted(bass_stem, lag);
        decoded = decode_mono(stems.other, stem_rate);
        if (!decoded) {
            return decoded.error();
        }
        Result<FrontEndResult> other_features =
            analyse_front_end(shifted(decoded.value(), lag), stem_rate, options.front_end);
        if (!other_features) {
            return other_features.error();
        }
        Result<ResidualFeatures> residual =
            analyse_residual(other_features.value(), options.residual);
        if (!residual) {
            return residual.error();
        }
        analysis.residual = std::move(residual.value());
        Result<FrontEndResult> features = analyse_front_end(drums, stem_rate, options.front_end);
        if (!features) {
            return features.error();
        }
        drums_features = std::move(features.value());
    }
    const FrontEndResult &beat_features =
        options.source == AnalysisSource::stems ? drums_features : analysis.mix_features;
    Result<BeatTrackResult> beats = track_beats(beat_features, options.beats);
    if (!beats) {
        return beats.error();
    }
    analysis.beats = std::move(beats.value());
    if (options.grid) {
        analysis.grid = *options.grid;
    } else if (analysis.beats.bpm > 0.0) {
        analysis.grid = analysis.beats.grid;
    } else {
        return Error{"no tempo found in " + path_to_utf8(audio_path.filename())};
    }

    if (options.source == AnalysisSource::stems) {
        const Result<BassTrackResult> drum_kicks =
            track_bass(drums, drums_features, analysis.grid, options.bass);
        if (!drum_kicks) {
            return drum_kicks.error();
        }
        Result<FrontEndResult> bass_features =
            analyse_front_end(bass_stem, analysis.sample_rate_hz, options.front_end);
        if (!bass_features) {
            return bass_features.error();
        }
        Result<BassTrackResult> notes = track_bass(bass_stem, bass_features.value(), analysis.grid,
                                                   options.bass, drum_kicks.value().kick_times_s);
        if (!notes) {
            return notes.error();
        }
        analysis.bass = std::move(notes.value());
    } else {
        Result<BassTrackResult> notes =
            track_bass(mix, analysis.mix_features, analysis.grid, options.bass);
        if (!notes) {
            return notes.error();
        }
        analysis.bass = std::move(notes.value());
    }

    Result<std::vector<DetectedSection>> sections =
        detect_sections(analysis.mix_features, analysis.beats, options.sections);
    if (!sections) {
        return sections.error();
    }
    analysis.sections = std::move(sections.value());

    // Hats from the beat source (mix or drums); the residual from the mix unless the "other" stem
    // gave it above.
    Result<ResidualFeatures> hats = analyse_residual(beat_features, options.residual);
    if (!hats) {
        return hats.error();
    }
    if (options.source == AnalysisSource::stems) {
        analysis.residual.hats = std::move(hats.value().hats);
    } else {
        analysis.residual = std::move(hats.value());
    }
    analysis.aggregates =
        aggregate_bass(analysis.bass.notes, analysis.grid, analysis.duration_s,
                       static_cast<int>(std::lround(options.beats.bars_per_phrase)),
                       analysis.beats.phrase_bar_offset);
    return analysis;
}

} // namespace psyight
