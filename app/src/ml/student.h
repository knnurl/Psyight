// The M4 student in the app (SPEC section 6.9; decision log 2026-10-17: hand-written inference,
// no runtime). Loads the weights tools/python/m4_export.py writes (batch norm folded), runs the
// causal TCN and decodes bass notes as tools/python/m4_decode.py does.
//
// Network: a 1x1 projection of the 192 student features, then residual layers h += gelu(conv(h))
// where conv is a 3-tap causal convolution with dilation d (taps at t - 2d, t - d, t), then two
// 1x1 heads: 5 logits (kick, hat and bass onset, bass gate, residual activity) and 44 pitch
// logits (MIDI 21 to 64). Output frame t describes input frame t - lookahead.
//
// Two paths share one per-frame kernel, so they give bit-identical outputs: StudentStream takes a
// frame at a time (live mode, causal, each layer keeps its last 2d + 1 inputs) and run_student
// takes a whole track (file mode, layer by layer, frames split across threads). Both start from
// zero history, as the training's left padding does. Every sum runs in one fixed order, so runs
// are deterministic. Latency: lookahead frames (2 frames, 10.7 ms, for the trained student) plus
// the feature hop.
//
// The weights are trained on private music: they live in data/m4/ (gitignored), never in the
// repository. Tests use a fixture with random weights (app/tests/data/student_fixture.*).
#pragma once

#include "analysis/bass_tracker.h"
#include "core/result.h"
#include "core/timebase.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace psyight {

inline constexpr int kStudentBinaryHeads = 5;
enum StudentHead { kKickOnset = 0, kHatOnset = 1, kBassOnset = 2, kBassGate = 3, kResidual = 4 };

// m4_decode.py's settings; the weights file carries the ones m4_tune.py chose.
struct StudentDecoderParams {
    double onset_threshold = 0.5;
    double gate_threshold = 0.5;
    int min_gap_frames = 8;
    int min_frames = 4;
    int kick_mask_frames = 0;
    double kick_threshold = 0.5;
    bool subframe = true;
};

struct StudentModel {
    int dims = 0;
    int channels = 0;
    std::vector<int> dilations;
    int lookahead = 0;
    int first_midi = 21;
    int pitches = 0;
    float features_scale = 1.0f / 127.0f; // Training saw features quantised to this step.
    StudentDecoderParams decoder;
    // Input-major: input_w[i * channels + o]; conv_w[l][(tap * channels + i) * channels + o].
    std::vector<float> input_w, input_b;
    std::vector<std::vector<float>> conv_w, conv_b;
    std::vector<float> binary_w, binary_b; // [channels][5], [5]
    std::vector<float> pitch_w, pitch_b;   // [channels][pitches], [pitches]

    int receptive_field() const;
};

// base: the path without extension; reads base.json and base.bin.
Result<StudentModel> load_student(const std::filesystem::path &base);

struct StudentOutputs {
    int frames = 0;
    int pitches = 0;
    std::vector<float> binary; // frames * 5 logits.
    std::vector<float> pitch;  // frames * pitches logits.
};

class StudentStream {
  public:
    explicit StudentStream(const StudentModel &model);
    void reset();
    // features: one frame (model.dims). Writes the logits of the frame lookahead frames ago.
    // No allocation.
    void process(std::span<const float> features, std::span<float> binary, std::span<float> pitch);

  private:
    const StudentModel &model_;
    std::vector<std::vector<float>> history_; // Per layer: the last 2d + 1 inputs, a ring.
    std::vector<int> position_;
    long long seen_ = 0; // Frames processed since reset.
    std::vector<float> h_, y_;
};

// features: frames * model.dims, frame-major. threads 0: the hardware's.
StudentOutputs run_student(const StudentModel &model, std::span<const float> features, int frames,
                           int threads = 0);

// Rounds features to the training's quantisation step (teacher files store them as int8).
void quantise_student_features(std::span<float> features, float scale);

struct StudentNote {
    double onset_frames = 0.0; // Input frames, fractional when refined.
    double end_frames = 0.0;
    int midi = 0;
    double onset_probability = 0.0;
};

std::vector<StudentNote> decode_student(const StudentOutputs &outputs, int lookahead,
                                        int first_midi, const StudentDecoderParams &params);

// As the tracker's notes: gate ratio, slot and long-note flag from the grid; velocity 1;
// confidence is the onset probability; no decay, slides or cents.
std::vector<BassNote> student_bass_notes(const std::vector<StudentNote> &notes, double frame_s,
                                         const Grid &grid);

struct StudentResult {
    StudentOutputs outputs;
    std::vector<BassNote> notes;
    double frame_s = 0.0;
};

// Mono audio to notes: features (as live mode would compute them), quantised, inference, decode.
Result<StudentResult> analyse_student(const StudentModel &model, std::span<const float> mono,
                                      int sample_rate_hz, const Grid &grid);

} // namespace psyight
