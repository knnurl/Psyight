#include "ml/student.h"

#include "analysis/student_data.h"
#include "core/paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numeric>
#include <thread>

namespace psyight {

namespace {

constexpr double kLongNoteSlots = 1.5; // As the tracker's default (bass.long_note_slots).

// out[o] = b[o] + sum over i of w[i][o] * x[i], accumulated in order of i. Vectorises across o
// without reordering any sum.
void affine(const float *w, const float *b, const float *x, int in, int out, float *y) {
    std::copy(b, b + out, y);
    for (int i = 0; i < in; ++i) {
        const float xi = x[i];
        const float *row = w + static_cast<size_t>(i) * out;
        for (int o = 0; o < out; ++o) {
            y[o] += row[o] * xi;
        }
    }
}

float gelu(float v) { return 0.5f * v * (1.0f + std::erf(v * 0.70710678118654752f)); }

// One residual layer at one frame: h_next = h + gelu(conv), taps oldest first. A null tap is a
// frame before the start (zero). The one kernel both paths use.
void layer_frame(const StudentModel &m, int layer, const float *tap0, const float *tap1,
                 const float *h, float *y, float *h_next) {
    const int c = m.channels;
    const float *w = m.conv_w[layer].data();
    std::copy(m.conv_b[layer].begin(), m.conv_b[layer].end(), y);
    const float *taps[3] = {tap0, tap1, h};
    for (int tap = 0; tap < 3; ++tap) {
        if (taps[tap] == nullptr) {
            continue;
        }
        for (int i = 0; i < c; ++i) {
            const float xi = taps[tap][i];
            const float *row = w + (static_cast<size_t>(tap) * c + i) * c;
            for (int o = 0; o < c; ++o) {
                y[o] += row[o] * xi;
            }
        }
    }
    for (int o = 0; o < c; ++o) {
        h_next[o] = h[o] + gelu(y[o]);
    }
}

void heads(const StudentModel &m, const float *h, float *binary, float *pitch) {
    affine(m.binary_w.data(), m.binary_b.data(), h, m.channels, kStudentBinaryHeads, binary);
    affine(m.pitch_w.data(), m.pitch_b.data(), h, m.channels, m.pitches, pitch);
}

// Runs body(first, last) over [0, count) split across threads.
template <typename Body> void parallel_for(int count, int threads, Body body) {
    threads = std::clamp(threads, 1, std::max(1, count / 256));
    if (threads == 1) {
        body(0, count);
        return;
    }
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        const int first = static_cast<int>(static_cast<long long>(count) * t / threads);
        const int last = static_cast<int>(static_cast<long long>(count) * (t + 1) / threads);
        workers.emplace_back(body, first, last);
    }
    for (std::thread &w : workers) {
        w.join();
    }
}

Result<std::vector<float>> read_array(const nlohmann::json &header, const std::string &bin,
                                      const std::string &name, size_t expected) {
    if (!header["arrays"].contains(name)) {
        return Error{"student weights: missing array " + name};
    }
    const nlohmann::json &a = header["arrays"][name];
    if (a.value("dtype", "") != "float32") {
        return Error{"student weights: " + name + " is not float32"};
    }
    size_t count = 1;
    for (const auto &d : a["shape"]) {
        count *= d.get<size_t>();
    }
    const size_t offset = a["offset"].get<size_t>();
    if (count != expected || offset + count * sizeof(float) > bin.size()) {
        return Error{"student weights: " + name + " has the wrong size"};
    }
    std::vector<float> out(count);
    std::memcpy(out.data(), bin.data() + offset, count * sizeof(float));
    return out;
}

float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

// As m4_decode.peaks: local maxima at or above the threshold, strongest first, at least min_gap
// apart, returned in time order.
std::vector<int> peaks(const std::vector<float> &p, float threshold, int min_gap) {
    std::vector<int> candidates;
    for (int k = 1; k + 1 < static_cast<int>(p.size()); ++k) {
        if (p[k] >= threshold && p[k] >= p[k - 1] && p[k] > p[k + 1]) {
            candidates.push_back(k);
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&p](int a, int b) { return p[a] > p[b]; });
    std::vector<int> kept;
    for (const int k : candidates) {
        if (std::all_of(kept.begin(), kept.end(),
                        [&](int j) { return std::abs(k - j) >= min_gap; })) {
            kept.push_back(k);
        }
    }
    std::sort(kept.begin(), kept.end());
    return kept;
}

float parabolic(const std::vector<float> &y, int k) {
    if (k <= 0 || k >= static_cast<int>(y.size()) - 1) {
        return 0.0f;
    }
    const float a = y[k - 1], b = y[k], c = y[k + 1];
    const float denominator = a - 2.0f * b + c;
    return denominator < 0.0f ? std::clamp(0.5f * (a - c) / denominator, -0.5f, 0.5f) : 0.0f;
}

} // namespace

int StudentModel::receptive_field() const {
    return 1 + 2 * std::accumulate(dilations.begin(), dilations.end(), 0);
}

Result<StudentModel> load_student(const std::filesystem::path &base) {
    const std::filesystem::path json_path = base.string() + ".json";
    const std::filesystem::path bin_path = base.string() + ".bin";
    std::ifstream json_file(json_path, std::ios::binary);
    std::ifstream bin_file(bin_path, std::ios::binary);
    if (!json_file || !bin_file) {
        return Error{"no student weights at " + path_to_utf8(json_path)};
    }
    const nlohmann::json h = nlohmann::json::parse(json_file, nullptr, false);
    if (h.is_discarded() || h.value("format", "") != "psyight-student" ||
        h.value("version", 0) != 1) {
        return Error{path_to_utf8(json_path) + " is not a version 1 student weights file"};
    }
    const std::string bin((std::istreambuf_iterator<char>(bin_file)), {});
    StudentModel m;
    try {
        m.dims = h.at("dims").get<int>();
        m.channels = h.at("channels").get<int>();
        m.dilations = h.at("dilations").get<std::vector<int>>();
        m.lookahead = h.at("lookahead").get<int>();
        m.first_midi = h.at("first_midi").get<int>();
        m.pitches = h.at("pitches").get<int>();
        m.features_scale = h.at("features_scale").get<float>();
        const nlohmann::json &d = h.at("decoder");
        m.decoder.onset_threshold = d.at("onset_threshold").get<double>();
        m.decoder.gate_threshold = d.at("gate_threshold").get<double>();
        m.decoder.min_gap_frames = d.at("min_gap_frames").get<int>();
        m.decoder.min_frames = d.at("min_frames").get<int>();
        m.decoder.kick_mask_frames = d.at("kick_mask_frames").get<int>();
        m.decoder.kick_threshold = d.at("kick_threshold").get<double>();
        m.decoder.subframe = d.at("subframe").get<bool>();
    } catch (const nlohmann::json::exception &e) {
        return Error{"student weights header: " + std::string(e.what())};
    }
    if (m.dims <= 0 || m.channels <= 0 || m.pitches <= 0 || m.dilations.empty() ||
        m.lookahead < 0 || m.decoder.min_frames < 1) {
        return Error{"student weights header: bad sizes"};
    }
    const auto c = static_cast<size_t>(m.channels);
    const auto load = [&](const std::string &name, size_t count, std::vector<float> &into) {
        Result<std::vector<float>> a = read_array(h, bin, name, count);
        if (a) {
            into = std::move(a.value());
            return std::string();
        }
        return a.error().message;
    };
    std::string problem = load("input_w", static_cast<size_t>(m.dims) * c, m.input_w);
    problem += load("input_b", c, m.input_b);
    m.conv_w.resize(m.dilations.size());
    m.conv_b.resize(m.dilations.size());
    for (size_t l = 0; l < m.dilations.size(); ++l) {
        problem += load("conv" + std::to_string(l) + "_w", 3 * c * c, m.conv_w[l]);
        problem += load("conv" + std::to_string(l) + "_b", c, m.conv_b[l]);
    }
    problem += load("binary_w", c * kStudentBinaryHeads, m.binary_w);
    problem += load("binary_b", kStudentBinaryHeads, m.binary_b);
    problem += load("pitch_w", c * m.pitches, m.pitch_w);
    problem += load("pitch_b", static_cast<size_t>(m.pitches), m.pitch_b);
    if (!problem.empty()) {
        return Error{problem};
    }
    return m;
}

StudentStream::StudentStream(const StudentModel &model)
    : model_(model), h_(static_cast<size_t>(2 * model.channels)),
      y_(static_cast<size_t>(model.channels)) {
    for (const int d : model.dilations) {
        history_.emplace_back(static_cast<size_t>((2 * d + 1) * model.channels));
    }
    position_.resize(model.dilations.size());
    reset();
}

void StudentStream::reset() {
    for (std::vector<float> &ring : history_) {
        std::fill(ring.begin(), ring.end(), 0.0f);
    }
    std::fill(position_.begin(), position_.end(), 0);
    seen_ = 0;
}

void StudentStream::process(std::span<const float> features, std::span<float> binary,
                            std::span<float> pitch) {
    const StudentModel &m = model_;
    const int c = m.channels;
    float *h = h_.data();
    float *h_next = h_.data() + c;
    affine(m.input_w.data(), m.input_b.data(), features.data(), m.dims, c, h);
    for (size_t l = 0; l < m.dilations.size(); ++l) {
        const int d = m.dilations[l];
        const int length = 2 * d + 1;
        std::vector<float> &ring = history_[l];
        // The ring holds this layer's inputs for frames t - 2d .. t; slot position_ is frame t.
        std::copy(h, h + c, ring.data() + static_cast<size_t>(position_[l]) * c);
        const auto frame = [&](int back) -> const float * {
            if (seen_ < back) {
                return nullptr; // Before the first frame: zero, as the training's padding.
            }
            const int slot = (position_[l] - back + length) % length;
            return ring.data() + static_cast<size_t>(slot) * c;
        };
        layer_frame(m, static_cast<int>(l), frame(2 * d), frame(d), h, y_.data(), h_next);
        position_[l] = (position_[l] + 1) % length;
        std::swap(h, h_next);
    }
    heads(m, h, binary.data(), pitch.data());
    ++seen_;
}

StudentOutputs run_student(const StudentModel &m, std::span<const float> features, int frames,
                           int threads) {
    if (threads <= 0) {
        threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    }
    const int c = m.channels;
    std::vector<float> h(static_cast<size_t>(frames) * c);
    std::vector<float> h_next(h.size());
    parallel_for(frames, threads, [&](int first, int last) {
        for (int t = first; t < last; ++t) {
            affine(m.input_w.data(), m.input_b.data(),
                   features.data() + static_cast<size_t>(t) * m.dims, m.dims, c,
                   h.data() + static_cast<size_t>(t) * c);
        }
    });
    for (size_t l = 0; l < m.dilations.size(); ++l) {
        const int d = m.dilations[l];
        parallel_for(frames, threads, [&](int first, int last) {
            std::vector<float> y(static_cast<size_t>(c));
            const auto at = [&](int t) {
                return t < 0 ? nullptr : h.data() + static_cast<size_t>(t) * c;
            };
            for (int t = first; t < last; ++t) {
                layer_frame(m, static_cast<int>(l), at(t - 2 * d), at(t - d), at(t), y.data(),
                            h_next.data() + static_cast<size_t>(t) * c);
            }
        });
        std::swap(h, h_next);
    }
    StudentOutputs out;
    out.frames = frames;
    out.pitches = m.pitches;
    out.binary.resize(static_cast<size_t>(frames) * kStudentBinaryHeads);
    out.pitch.resize(static_cast<size_t>(frames) * m.pitches);
    parallel_for(frames, threads, [&](int first, int last) {
        for (int t = first; t < last; ++t) {
            heads(m, h.data() + static_cast<size_t>(t) * c,
                  out.binary.data() + static_cast<size_t>(t) * kStudentBinaryHeads,
                  out.pitch.data() + static_cast<size_t>(t) * m.pitches);
        }
    });
    return out;
}

void quantise_student_features(std::span<float> features, float scale) {
    for (float &v : features) {
        const auto q = static_cast<float>(std::lround(std::clamp(v, -1.0f, 1.0f) * 127.0f));
        v = q * scale;
    }
}

std::vector<StudentNote> decode_student(const StudentOutputs &outputs, int lookahead,
                                        int first_midi, const StudentDecoderParams &p) {
    const int frames = outputs.frames;
    std::vector<float> onset(frames), gate(frames), kick(frames);
    std::vector<int> classes(frames);
    for (int t = 0; t < frames; ++t) {
        const float *b = outputs.binary.data() + static_cast<size_t>(t) * kStudentBinaryHeads;
        kick[t] = sigmoid(b[kKickOnset]);
        onset[t] = sigmoid(b[kBassOnset]);
        gate[t] = sigmoid(b[kBassGate]);
        const float *row = outputs.pitch.data() + static_cast<size_t>(t) * outputs.pitches;
        classes[t] = static_cast<int>(std::max_element(row, row + outputs.pitches) - row);
    }
    // Thresholds compare in float, as numpy does with float32 arrays.
    std::vector<int> found = peaks(onset, static_cast<float>(p.onset_threshold), p.min_gap_frames);
    if (p.kick_mask_frames > 0) {
        const std::vector<int> kicks = peaks(kick, static_cast<float>(p.kick_threshold), 4);
        std::erase_if(found, [&](int k) {
            return std::any_of(kicks.begin(), kicks.end(),
                               [&](int j) { return std::abs(j - k) <= p.kick_mask_frames; });
        });
    }
    const float gate_threshold = static_cast<float>(p.gate_threshold);
    std::vector<StudentNote> notes;
    std::vector<int> counts(static_cast<size_t>(outputs.pitches));
    for (size_t n = 0; n < found.size(); ++n) {
        const int k = found[n];
        const int stop = n + 1 < found.size() ? found[n + 1] : frames;
        int end = std::min(stop, k + p.min_frames);
        while (end < stop && gate[end] >= gate_threshold) {
            ++end;
        }
        std::fill(counts.begin(), counts.end(), 0);
        for (int t = k; t < end; ++t) {
            ++counts[classes[t]];
        }
        const int midi =
            static_cast<int>(std::max_element(counts.begin(), counts.end()) - counts.begin()) +
            first_midi;
        double start = k;
        double finish = end;
        if (p.subframe) {
            start += parabolic(onset, k);
            if (end < stop && end > 0 && gate[end - 1] > gate[end]) {
                finish = (end - 1) + (gate[end - 1] - gate_threshold) / (gate[end - 1] - gate[end]);
            }
        }
        notes.push_back({start - lookahead, finish - lookahead, midi, onset[k]});
    }
    return notes;
}

std::vector<BassNote> student_bass_notes(const std::vector<StudentNote> &notes, double frame_s,
                                         const Grid &grid) {
    const double slot_s = grid.slot_length_samples() / grid.sample_rate_hz;
    std::vector<BassNote> out;
    for (const StudentNote &s : notes) {
        BassNote note;
        note.onset_s = std::max(0.0, s.onset_frames * frame_s);
        note.offset_s = std::max(note.onset_s, s.end_frames * frame_s);
        note.duration_ms = 1e3 * (note.offset_s - note.onset_s);
        note.slot_len_ms = 1e3 * slot_s;
        note.gate_ratio = (note.offset_s - note.onset_s) / slot_s;
        note.pitch_midi = s.midi;
        note.octave = s.midi / 12 - 1;
        note.is_long = note.offset_s - note.onset_s > kLongNoteSlots * slot_s;
        note.velocity = 1.0;
        note.confidence = s.onset_probability;
        note.slot_index = grid.slot_in_bar(
            grid.snap_to_slot(seconds_to_samples(note.onset_s, grid.sample_rate_hz)));
        out.push_back(note);
    }
    return out;
}

Result<StudentResult> analyse_student(const StudentModel &model, std::span<const float> mono,
                                      int sample_rate_hz, const Grid &grid) {
    const StudentFeatureParams params;
    Result<StudentFeatures> features = student_features(mono, sample_rate_hz, params);
    if (!features) {
        return features.error();
    }
    StudentFeatures &f = features.value();
    if (f.dims != model.dims) {
        return Error{"student weights expect " + std::to_string(model.dims) + " features, not " +
                     std::to_string(f.dims)};
    }
    quantise_student_features(f.values, model.features_scale);
    StudentResult result;
    result.frame_s = static_cast<double>(params.hop_samples) / sample_rate_hz;
    result.outputs = run_student(model, f.values, f.frames);
    result.notes = student_bass_notes(
        decode_student(result.outputs, model.lookahead, model.first_midi, model.decoder),
        result.frame_s, grid);
    return result;
}

} // namespace psyight
