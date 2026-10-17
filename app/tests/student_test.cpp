// The app's own student inference (ml/student.h) against onnxruntime and m4_decode.py, on a
// fixture with random weights (tools/python/m4_export.py fixture; nothing in it comes from music).
// The timing case is skipped by default:
//   psyight_tests -tc="student: inference cost" --no-skip

#include "ml/student.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace psyight;

namespace {

const std::filesystem::path kFixture =
    std::filesystem::path(PSYIGHT_TESTS_DIR) / "data" / "student_fixture";

struct Fixture {
    StudentModel model;
    int frames = 0;
    std::vector<float> features; // Quantised, frames * dims.
    StudentOutputs onnx;         // onnxruntime's logits.
    nlohmann::json notes;        // m4_decode.py's notes from them: [onset, end, midi].
};

template <typename T>
std::vector<T> array_of(const nlohmann::json &header, const std::string &bin,
                        const std::string &name) {
    const nlohmann::json &a = header["arrays"][name];
    size_t count = 1;
    for (const auto &d : a["shape"]) {
        count *= d.get<size_t>();
    }
    std::vector<T> out(count);
    std::memcpy(out.data(), bin.data() + a["offset"].get<size_t>(), count * sizeof(T));
    return out;
}

const Fixture &fixture() {
    static const Fixture f = [] {
        Fixture x;
        Result<StudentModel> model = load_student(kFixture);
        REQUIRE_MESSAGE(model.ok(), (model.ok() ? "" : model.error().message));
        x.model = std::move(model.value());
        std::ifstream json_file(kFixture.string() + ".json", std::ios::binary);
        std::ifstream bin_file(kFixture.string() + ".bin", std::ios::binary);
        const nlohmann::json header = nlohmann::json::parse(json_file);
        const std::string bin((std::istreambuf_iterator<char>(bin_file)), {});
        x.frames = header["fixture"]["frames"].get<int>();
        x.notes = header["fixture"]["notes"];
        for (const std::int8_t q : array_of<std::int8_t>(header, bin, "fixture_input")) {
            x.features.push_back(static_cast<float>(q) * x.model.features_scale);
        }
        x.onnx.frames = x.frames;
        x.onnx.pitches = x.model.pitches;
        x.onnx.binary = array_of<float>(header, bin, "fixture_binary");
        x.onnx.pitch = array_of<float>(header, bin, "fixture_pitch");
        return x;
    }();
    return f;
}

float largest_difference(const std::vector<float> &a, const std::vector<float> &b) {
    REQUIRE(a.size() == b.size());
    float worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

// A model of the trained student's size with random weights, for timing.
StudentModel random_model(int channels) {
    StudentModel m;
    m.dims = 192;
    m.channels = channels;
    m.dilations = {1, 2, 4, 8, 16, 32, 1, 2, 4, 8, 16, 32};
    m.lookahead = 2;
    m.pitches = 44;
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-0.05f, 0.05f);
    const auto fill = [&](size_t n) {
        std::vector<float> v(n);
        for (float &x : v) {
            x = u(rng);
        }
        return v;
    };
    const auto c = static_cast<size_t>(channels);
    m.input_w = fill(192 * c);
    m.input_b = fill(c);
    for (size_t l = 0; l < m.dilations.size(); ++l) {
        m.conv_w.push_back(fill(3 * c * c));
        m.conv_b.push_back(fill(c));
    }
    m.binary_w = fill(c * kStudentBinaryHeads);
    m.binary_b = fill(kStudentBinaryHeads);
    m.pitch_w = fill(c * 44);
    m.pitch_b = fill(44);
    return m;
}

} // namespace

TEST_CASE("student: the fixture's weights load with the decoder settings") {
    const StudentModel &m = fixture().model;
    CHECK(m.dims == 192);
    CHECK(m.channels == 8);
    CHECK(m.dilations.size() == 12);
    CHECK(m.receptive_field() == 253);
    CHECK(m.lookahead == 2);
    CHECK(m.decoder.onset_threshold == 0.4);
    CHECK(m.decoder.kick_mask_frames == 2);
    CHECK_FALSE(load_student(kFixture.parent_path() / "no such weights").ok());
}

TEST_CASE("student: whole-track inference matches onnxruntime") {
    const Fixture &f = fixture();
    const StudentOutputs out = run_student(f.model, f.features, f.frames);
    REQUIRE(out.frames == f.frames);
    // Different summation orders: agreement to float rounding, not bit for bit.
    CHECK(largest_difference(out.binary, f.onnx.binary) < 2e-5f);
    CHECK(largest_difference(out.pitch, f.onnx.pitch) < 2e-5f);
}

TEST_CASE("student: streaming, whole-track and any thread count are bit-identical") {
    const Fixture &f = fixture();
    const StudentOutputs one = run_student(f.model, f.features, f.frames, 1);
    const StudentOutputs many = run_student(f.model, f.features, f.frames, 7);
    CHECK(std::memcmp(one.binary.data(), many.binary.data(), one.binary.size() * 4) == 0);
    CHECK(std::memcmp(one.pitch.data(), many.pitch.data(), one.pitch.size() * 4) == 0);

    StudentStream stream(f.model);
    std::vector<float> binary(kStudentBinaryHeads);
    std::vector<float> pitch(static_cast<size_t>(f.model.pitches));
    int different = 0;
    for (int pass = 0; pass < 2; ++pass) { // The second pass checks reset().
        stream.reset();
        for (int t = 0; t < f.frames; ++t) {
            stream.process(
                std::span(f.features).subspan(static_cast<size_t>(t) * f.model.dims, f.model.dims),
                binary, pitch);
            different += std::memcmp(binary.data(), one.binary.data() + t * kStudentBinaryHeads,
                                     binary.size() * 4) != 0;
            different += std::memcmp(pitch.data(), one.pitch.data() + t * f.model.pitches,
                                     pitch.size() * 4) != 0;
        }
    }
    CHECK(different == 0);
}

TEST_CASE("student: decoding matches m4_decode.py") {
    const Fixture &f = fixture();
    const std::vector<StudentNote> notes =
        decode_student(f.onnx, f.model.lookahead, f.model.first_midi, f.model.decoder);
    REQUIRE(notes.size() == f.notes.size());
    REQUIRE(notes.size() >= 10);
    for (size_t i = 0; i < notes.size(); ++i) {
        CAPTURE(i);
        CHECK(notes[i].onset_frames == doctest::Approx(f.notes[i][0].get<double>()).epsilon(1e-5));
        CHECK(notes[i].end_frames == doctest::Approx(f.notes[i][1].get<double>()).epsilon(1e-5));
        CHECK(notes[i].midi == f.notes[i][2].get<int>());
    }
    // The app's own logits decode to the same notes.
    const std::vector<StudentNote> own =
        decode_student(run_student(f.model, f.features, f.frames), f.model.lookahead,
                       f.model.first_midi, f.model.decoder);
    REQUIRE(own.size() == notes.size());
    for (size_t i = 0; i < own.size(); ++i) {
        CHECK(own[i].midi == notes[i].midi);
        CHECK(own[i].onset_frames == doctest::Approx(notes[i].onset_frames).epsilon(1e-4));
    }
}

TEST_CASE("student: notes on the grid") {
    const Grid grid{150.0, 0, 48000}; // 16ths of 100 ms.
    const std::vector<BassNote> notes =
        student_bass_notes({{75.0, 90.0, 28, 0.8}, {93.75, 150.0, 40, 0.6}}, 256.0 / 48000.0, grid);
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].onset_s == doctest::Approx(0.4));
    CHECK(notes[0].gate_ratio == doctest::Approx(0.8));
    CHECK(notes[0].slot_index == 4);
    CHECK_FALSE(notes[0].is_long);
    CHECK(notes[0].octave == 1);
    CHECK(notes[0].confidence == doctest::Approx(0.8));
    CHECK(notes[1].onset_s == doctest::Approx(0.5));
    CHECK(notes[1].is_long); // 0.3 s, 3 slots.
}

TEST_CASE("student: inference cost" * doctest::skip()) {
    const StudentModel m = random_model(96); // The trained student's size.
    const int frames = 80000;                // A 7-minute track at 5.33 ms.
    std::vector<float> features(static_cast<size_t>(frames) * m.dims, 0.1f);
    StudentStream stream(m);
    std::vector<float> binary(kStudentBinaryHeads), pitch(44);
    std::vector<double> times_us;
    for (int t = 0; t < 3000; ++t) {
        const auto start = std::chrono::steady_clock::now();
        stream.process(std::span(features).subspan(0, m.dims), binary, pitch);
        times_us.push_back(
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
                .count());
    }
    std::sort(times_us.begin(), times_us.end());
    const auto start = std::chrono::steady_clock::now();
    const StudentOutputs out = run_student(m, features, frames);
    const double track_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    MESSAGE("streaming, one frame: median "
            << times_us[times_us.size() / 2] << " us, p99 " << times_us[times_us.size() * 99 / 100]
            << " us; whole track (" << frames << " frames): " << track_s << " s");
    CHECK(out.frames == frames);
}
