// psyight_teacher: exports one track's M4 training data (SPEC 6.9): the causal student features of
// the mix and the teacher targets from the file-mode analysis (from the Demucs stems with
// --stems), or only a fingerprint (--fingerprint-only) for finding duplicates.
//
//   psyight_teacher [--stems] [--fingerprint-only] <audio> <output base>
//
// Writes <output base>.json (description: frames, dims, hop, tempo, and each array's offset, type
// and shape in the .bin) and <output base>.bin (the arrays, little-endian, back to back), which
// numpy reads directly. Never writes next to the audio; stems are looked for in the usual place
// (io/stems.h) relative to the audio, so the dataset builder works on a copy in a scratch folder.

#include "analysis/student_data.h"
#include "analysis/track_analysis.h"
#include "audio/audio_file.h"
#include "core/paths.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace psyight;

namespace {

struct Writer {
    std::vector<char> bytes;
    nlohmann::json arrays = nlohmann::json::object();

    template <typename T>
    void add(const char *name, const std::vector<T> &values, std::vector<int> shape,
             const char *dtype) {
        arrays[name] = {{"offset", bytes.size()}, {"dtype", dtype}, {"shape", shape}};
        const auto *begin = reinterpret_cast<const char *>(values.data());
        bytes.insert(bytes.end(), begin, begin + values.size() * sizeof(T));
    }
};

int run(const std::filesystem::path &audio, const std::filesystem::path &out, bool stems,
        bool fingerprint_only) {
    const auto fail = [&audio](const std::string &message) {
        std::fprintf(stderr, "%s: %s\n", path_to_utf8(audio.filename()).c_str(), message.c_str());
        return 1;
    };
    const Result<AudioBuffer> decoded = decode_audio_file(audio, kAnalysisSampleRateHz);
    if (!decoded) {
        return fail(decoded.error().message);
    }
    const std::vector<float> mono = mono_mix(decoded.value());
    const StudentFeatureParams params;
    const Result<StudentFeatures> features = student_features(mono, kAnalysisSampleRateHz, params);
    if (!features) {
        return fail(features.error().message);
    }
    const StudentFeatures &f = features.value();
    Writer w;
    const std::vector<float> print = fingerprint(f, kAnalysisSampleRateHz, params);
    w.add("fingerprint", print,
          {static_cast<int>(print.size()) / kFingerprintBands, kFingerprintBands}, "<f4");
    nlohmann::json meta = {{"schema", 1},
                           {"name", path_to_utf8(audio.filename())},
                           {"sample_rate_hz", kAnalysisSampleRateHz},
                           {"hop_samples", params.hop_samples},
                           {"frames", f.frames},
                           {"dims", f.dims},
                           {"short_bands", params.short_bands},
                           {"long_bands", params.long_bands}};
    if (!fingerprint_only) {
        TrackAnalysisOptions options;
        options.source = stems ? AnalysisSource::stems : AnalysisSource::mix;
        const Result<TrackAnalysis> analysis =
            analyse_track(mono, kAnalysisSampleRateHz, audio, options);
        if (!analysis) {
            return fail(analysis.error().message);
        }
        const TrackAnalysis &a = analysis.value();
        const TeacherTargets t = teacher_targets(a, f.frames, params.hop_samples);
        // Features are in -1..1: 8 bits (steps of 1/127, about 0.47 dB) keep ~200 tracks in a
        // few GB. Read back as value = byte / 127.
        std::vector<std::int8_t> quantised(f.values.size());
        for (size_t i = 0; i < f.values.size(); ++i) {
            quantised[i] = static_cast<std::int8_t>(std::lround(f.values[i] * 127.0f));
        }
        w.add("features", quantised, {f.frames, f.dims}, "|i1");
        meta["features_scale"] = 1.0 / 127.0;
        w.add("kick_onset", t.kick_onset, {f.frames}, "<f4");
        w.add("hat_onset", t.hat_onset, {f.frames}, "<f4");
        w.add("bass_onset", t.bass_onset, {f.frames}, "<f4");
        w.add("bass_gate", t.bass_gate, {f.frames}, "<f4");
        w.add("bass_pitch", t.bass_pitch, {f.frames}, "<i2");
        w.add("residual_activity", t.residual_activity, {f.frames}, "<f4");
        meta["source"] = stems ? "stems" : "mix";
        meta["bpm"] = a.grid.bpm;
        meta["tempo_confidence"] = a.beats.tempo_confidence;
        meta["kicks"] = a.bass.kick_times_s.size();
        meta["notes"] = a.bass.notes.size();
        meta["sections"] = a.sections.size();
    }
    meta["arrays"] = w.arrays;
    std::filesystem::path bin = out;
    bin += ".bin";
    std::filesystem::path json = out;
    json += ".json";
    std::ofstream(bin, std::ios::binary)
        .write(w.bytes.data(), static_cast<std::streamsize>(w.bytes.size()));
    std::ofstream(json, std::ios::binary) << meta.dump(2) << "\n";
    std::printf("%s: %d frames%s\n", path_to_utf8(audio.filename()).c_str(), f.frames,
                fingerprint_only ? " (fingerprint)" : "");
    return 0;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    bool stems = false;
    bool fingerprint_only = false;
    std::vector<std::wstring> paths;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--stems") == 0) {
            stems = true;
        } else if (std::wcscmp(argv[i], L"--fingerprint-only") == 0) {
            fingerprint_only = true;
        } else {
            paths.emplace_back(argv[i]);
        }
    }
    if (paths.size() != 2) {
        std::fprintf(stderr, "usage: psyight_teacher [--stems] [--fingerprint-only] <audio> "
                             "<output base>\n");
        return 2;
    }
    return run(paths[0], paths[1], stems, fingerprint_only);
}
