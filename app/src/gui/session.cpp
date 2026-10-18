#include "gui/session.h"

#include "core/paths.h"
#include "io/stems.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace psyight {

namespace {

constexpr int kOverviewBuckets = 4000;
constexpr auto kAutosaveInterval = std::chrono::seconds(60);
constexpr double kPlayheadLead = 0.3; // Fraction of the view shown before the playhead.
constexpr double kMinViewS = 0.25;
constexpr double kMaxViewS = 120.0;

} // namespace

void Session::open_file(const std::filesystem::path &path) {
    if (loading_.valid()) {
        status_ = "Still loading " + loading_name_;
        return;
    }
    loading_name_ = path_to_utf8(path.filename());
    status_ = "Loading and analysing " + loading_name_ + "...";
    loading_ = std::async(std::launch::async, &Session::load, path, student_model_);
}

std::shared_ptr<const StudentModel> Session::load_student_model() {
    Result<StudentModel> model = load_student("data/m4/student.weights");
    if (!model) {
        spdlog::info("M4 student not loaded: {}", model.error().message);
        return nullptr;
    }
    spdlog::info("M4 student loaded: {} channels, {} layers, lookahead {} frames",
                 model.value().channels, model.value().dilations.size(), model.value().lookahead);
    return std::make_shared<const StudentModel>(std::move(model.value()));
}

const std::vector<BassNote> *Session::detected_notes() const {
    if (!has_track()) {
        return nullptr;
    }
    if (detection_source == DetectionSource::student && track_->student != nullptr) {
        return &track_->student->notes;
    }
    return track_->track_analysis != nullptr ? &track_->track_analysis->bass.notes : nullptr;
}

const char *Session::detection_name() const {
    if (detection_source == DetectionSource::student && student_available()) {
        return "M4 student";
    }
    return has_track() && track_->track_analysis != nullptr &&
                   track_->track_analysis->source == AnalysisSource::stems
               ? "stems"
               : "mix";
}

Session::LoadResult Session::load(std::filesystem::path path,
                                  std::shared_ptr<const StudentModel> student) {
    LoadResult result;
    result.audio = decode_audio_file(path);
    if (!result.audio) {
        return result;
    }
    const AudioBuffer &buffer = result.audio.value();
    auto track = std::make_shared<LoadedTrack>();
    track->name = path_to_utf8(path.filename());
    track->path = path;
    track->sample_rate_hz = buffer.sample_rate_hz;
    track->duration_s = samples_to_seconds(buffer.frames(), buffer.sample_rate_hz);

    const std::vector<float> mono = mono_mix(buffer);
    const auto frames = static_cast<SampleIndex>(mono.size());
    const int buckets = static_cast<int>(std::min<SampleIndex>(kOverviewBuckets, frames));
    for (int b = 0; b < buckets; ++b) {
        const SampleIndex begin = frames * b / buckets;
        const SampleIndex end = frames * (b + 1) / buckets;
        const auto [low, high] = std::minmax_element(mono.begin() + begin, mono.begin() + end);
        track->overview_time_s.push_back(
            samples_to_seconds((begin + end) / 2, buffer.sample_rate_hz));
        track->overview_min.push_back(std::min(0.0f, *low));
        track->overview_max.push_back(std::max(0.0f, *high));
    }

    // The whole file-mode analysis: from the Demucs stems when they exist, on the labelled grid
    // when the track has a labels file (else the beat tracker's). Without a tempo (and no labels)
    // only the front end runs, so the views still work.
    const auto start = std::chrono::steady_clock::now();
    TrackAnalysisOptions options;
    options.source =
        analysis_stems_exist(stem_paths_for(path)) ? AnalysisSource::stems : AnalysisSource::mix;
    if (std::filesystem::exists(labels_path_for(path))) {
        if (const Result<Labels> labels = load_labels(labels_path_for(path)); labels) {
            options.grid = Grid{
                labels.value().grid.bpm,
                seconds_to_samples(labels.value().grid.first_downbeat_s, buffer.sample_rate_hz),
                buffer.sample_rate_hz};
        }
    }
    Result<TrackAnalysis> analysis = analyse_track(mono, buffer.sample_rate_hz, path, options);
    if (!analysis && options.source == AnalysisSource::stems) {
        spdlog::warn("Stems analysis of {} failed ({}); analysing the mix", track->name,
                     analysis.error().message);
        options.source = AnalysisSource::mix;
        analysis = analyse_track(mono, buffer.sample_rate_hz, path, options);
    }
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (analysis) {
        const auto shared = std::make_shared<const TrackAnalysis>(std::move(analysis.value()));
        track->track_analysis = shared;
        track->analysis = std::shared_ptr<const FrontEndResult>(shared, &shared->mix_features);
        spdlog::info("Analysed {} from the {} in {:.0f} ms: {:.3f} BPM, {} bass notes, {} kicks, "
                     "{} sections",
                     track->name, options.source == AnalysisSource::stems ? "stems" : "mix",
                     elapsed_ms, shared->beats.bpm, shared->bass.notes.size(),
                     shared->bass.kick_times_s.size(), shared->sections.size());
        if (student != nullptr) { // On the mix, as live mode would hear it.
            const auto student_start = std::chrono::steady_clock::now();
            Result<StudentResult> notes =
                analyse_student(*student, mono, buffer.sample_rate_hz, shared->grid);
            if (notes) {
                track->student = std::make_shared<const StudentResult>(std::move(notes.value()));
                spdlog::info("M4 student on {}: {} bass notes in {:.0f} ms", track->name,
                             track->student->notes.size(),
                             std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - student_start)
                                 .count());
            } else {
                spdlog::warn("M4 student on {} failed: {}", track->name, notes.error().message);
            }
        }
    } else {
        spdlog::warn("Analysis of {} incomplete: {}", track->name, analysis.error().message);
        Result<FrontEndResult> features = analyse_front_end(mono, buffer.sample_rate_hz, {});
        if (features) {
            track->analysis = std::make_shared<const FrontEndResult>(std::move(features.value()));
        } else {
            spdlog::error("Analysis of {} failed: {}", track->name, features.error().message);
        }
    }
    result.track = std::move(track);
    return result;
}

Session::~Session() {
    stop_live();
    autosave("on exit");
}

Result<void> Session::start_live(const CaptureOptions &options) {
    stop_live();
    auto live = std::make_shared<LiveInput>(student_model_);
    if (Result<void> r = live->start(options); !r) {
        return r;
    }
    transport_.pause();
    live_owner_ = live;
    live_.store(live);
    spdlog::info("Live input started ({})",
                 options.source == CaptureSource::loopback ? std::string("loopback")
                 : options.source == CaptureSource::file
                     ? "file " + path_to_utf8(options.file.filename()) + ", silent"
                     : options.device_name);
    return {};
}

void Session::stop_live() {
    if (live_owner_ == nullptr) {
        return;
    }
    live_.store(nullptr);
    live_owner_->stop(); // Readers holding the pointer see a stopped clock until they let go.
    live_owner_.reset();
    spdlog::info("Live input stopped");
}

void Session::autosave(const char *reason) {
    if (!has_track() || !labels_.dirty()) {
        return;
    }
    if (const Result<void> saved = save_labels(); saved) {
        spdlog::info("Saved labels {} ({})", path_to_utf8(labels_path().filename()), reason);
    } else {
        spdlog::error("Autosave failed: {}", saved.error().message);
        last_save_ = std::chrono::steady_clock::now(); // Retry after the interval, not per frame.
    }
}

Result<void> Session::save_labels() {
    if (labels_file_unreadable_) {
        return Error{"not saving: " + path_to_utf8(labels_path()) +
                     " could not be read, so saving would overwrite it. Fix or move it first."};
    }
    const Result<void> saved = psyight::save_labels(labels_path(), labels_.labels());
    if (saved) {
        labels_.mark_saved();
        last_save_ = std::chrono::steady_clock::now();
    }
    return saved;
}

std::shared_ptr<const SectionTimeline> Session::edited_sections() {
    const Labels &l = labels_.labels();
    if (!has_track() || track_->track_analysis == nullptr || !l.sections_edited ||
        l.sections.empty()) {
        timeline_.reset();
        return nullptr;
    }
    const Grid g = grid();
    if (timeline_ != nullptr && timeline_from_ == l.sections && timeline_bpm_ == g.bpm &&
        timeline_downbeat_ == g.first_downbeat &&
        timeline_analysis_ == track_->track_analysis.get()) {
        return timeline_;
    }
    std::vector<std::pair<double, std::string>> starts;
    for (const SectionLabel &s : l.sections) {
        starts.emplace_back(s.start_s, s.label);
    }
    const TrackAnalysis &analysis = *track_->track_analysis;
    timeline_ = std::make_shared<const SectionTimeline>(section_timeline(
        analysis, g, sections_from_starts(std::move(starts), analysis.duration_s)));
    timeline_from_ = l.sections;
    timeline_bpm_ = g.bpm;
    timeline_downbeat_ = g.first_downbeat;
    timeline_analysis_ = track_->track_analysis.get();
    return timeline_;
}

std::filesystem::path Session::labels_path() const { return labels_path_for(track_->path); }

double Session::playhead_s() const {
    return has_track() ? samples_to_seconds(transport_.position_now(), track_->sample_rate_hz)
                       : 0.0;
}

Grid Session::grid() const {
    const int sample_rate_hz = has_track() ? track_->sample_rate_hz : kAnalysisSampleRateHz;
    const LabelGrid &grid = labels_.labels().grid;
    return {grid.bpm, seconds_to_samples(grid.first_downbeat_s, sample_rate_hz), sample_rate_hz};
}

void Session::set_loop(const LoopRegion &loop) {
    loop_ = loop;
    if (!has_track()) {
        return;
    }
    const int sr = track_->sample_rate_hz;
    const double start_s = std::min(loop.start_s, loop.end_s);
    const double end_s = std::max(loop.start_s, loop.end_s);
    transport_.set_loop(loop.enabled ? seconds_to_samples(start_s, sr) : 0,
                        loop.enabled ? seconds_to_samples(end_s, sr) : 0);
}

void Session::end_frame() {
    if (!view.follow_playhead ||
        (view.min_s == expected_view_min_s_ && view.max_s == expected_view_max_s_)) {
        return;
    }
    const double width_s = view.max_s - view.min_s;
    const double expected_width_s = expected_view_max_s_ - expected_view_min_s_;
    if (std::abs(width_s - expected_width_s) > 1e-6 * expected_width_s) {
        view.window_s = std::clamp(width_s, kMinViewS, kMaxViewS); // Zoom: keep following.
    } else {
        view.follow_playhead = false; // Pan: the user wants to look elsewhere.
    }
}

bool Session::stems_available() const {
    return has_track() && analysis_stems_exist(stem_paths_for(track_->path));
}

void Session::reanalyse(AnalysisSource source) {
    if (!has_track() || reanalysis_.valid() || loading_.valid()) {
        return;
    }
    TrackAnalysisOptions options;
    options.source = source;
    const LabelGrid &grid = labels_.labels().grid;
    options.grid = Grid{grid.bpm, seconds_to_samples(grid.first_downbeat_s, track_->sample_rate_hz),
                        track_->sample_rate_hz};
    reanalysis_track_ = track_.get();
    status_ = "Re-analysing " + track_->name + "...";
    reanalysis_ = std::async(std::launch::async, [path = track_->path, options] {
        return analyse_track(path, options);
    });
}

void Session::update() {
    if (reanalysis_.valid() &&
        reanalysis_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        Result<TrackAnalysis> analysis = reanalysis_.get();
        if (!has_track() || track_.get() != reanalysis_track_) {
            // The track changed meanwhile: drop the result.
        } else if (analysis) {
            const auto shared = std::make_shared<const TrackAnalysis>(std::move(analysis.value()));
            auto updated = std::make_shared<LoadedTrack>(*track_);
            updated->track_analysis = shared;
            if (track_->student != nullptr && student_model_ != nullptr) {
                // The same outputs on the new grid (gate ratios and slots follow the grid).
                auto student = std::make_shared<StudentResult>(*track_->student);
                student->notes = student_bass_notes(
                    decode_student(student->outputs, student_model_->lookahead,
                                   student_model_->first_midi, student_model_->decoder),
                    student->frame_s, shared->grid);
                updated->student = std::move(student);
            }
            updated->analysis =
                std::shared_ptr<const FrontEndResult>(shared, &shared->mix_features);
            track_ = std::move(updated);
            status_.clear();
            spdlog::info("Re-analysed {} from the {}: {} bass notes, {} kicks, {} sections",
                         track_->name, shared->source == AnalysisSource::stems ? "stems" : "mix",
                         shared->bass.notes.size(), shared->bass.kick_times_s.size(),
                         shared->sections.size());
        } else {
            status_ = "Re-analysis failed: " + analysis.error().message;
            spdlog::error("{}", status_);
        }
    }
    if (view.follow_playhead) {
        view.min_s = playhead_s() - kPlayheadLead * view.window_s;
        view.max_s = view.min_s + view.window_s;
    }
    expected_view_min_s_ = view.min_s;
    expected_view_max_s_ = view.max_s;
    if (labels_.dirty() && std::chrono::steady_clock::now() - last_save_ > kAutosaveInterval) {
        autosave("periodic");
    }
    if (!loading_.valid() ||
        loading_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    LoadResult result = loading_.get();
    if (!result.audio) {
        status_ = result.audio.error().message;
        spdlog::error("{}", status_);
        return;
    }
    autosave("switching track");
    const Result<void> opened = transport_.open(std::move(result.audio.value()));
    if (!opened) {
        status_ = opened.error().message;
        spdlog::error("{}", status_);
        return;
    }
    track_ = std::move(result.track);
    loop_ = {};
    status_.clear();
    spdlog::info("Loaded {} ({:.2f} s), device period {} frames", track_->name, track_->duration_s,
                 transport_.device_period_frames());

    // Labels: load the file next to the audio, or start empty.
    labels_file_unreadable_ = false;
    if (std::filesystem::exists(labels_path())) {
        Result<Labels> loaded = load_labels(labels_path());
        if (loaded) {
            labels_ = LabelEditor(std::move(loaded.value()));
            spdlog::info("Loaded {} labelled notes from {}", labels_.labels().bass_notes.size(),
                         path_to_utf8(labels_path().filename()));
            return;
        }
        // Keep the unreadable file untouched: start empty, and refuse to save over it.
        labels_file_unreadable_ = true;
        status_ = "Labels not loaded: " + loaded.error().message;
        spdlog::error("{}", status_);
    }
    Labels fresh;
    fresh.audio = track_->name;
    fresh.sample_rate = track_->sample_rate_hz;
    if (track_->track_analysis != nullptr && track_->track_analysis->beats.bpm > 0.0) {
        // Start from the detected grid (not saved until something is edited).
        const BeatTrackResult &beats = track_->track_analysis->beats;
        fresh.grid = {beats.bpm,
                      samples_to_seconds(beats.grid.first_downbeat, beats.grid.sample_rate_hz)};
    }
    labels_ = LabelEditor(std::move(fresh));
}

} // namespace psyight