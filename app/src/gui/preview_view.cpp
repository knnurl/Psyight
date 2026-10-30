#include "gui/preview_view.h"

#include "audio/audio_file.h"
#include "core/paths.h"
#include "core/playhead_clock.h"
#include "engine/effect_spec.h"
#include "gui/icons.h"
#include "gui/layout.h"
#include "gui/stage_canvas.h"
#include "gui/theme.h"
#include "gui/tier.h"
#include "gui/widgets.h"
#include "io/session_record.h"

#include <ctime>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace psyight {

namespace {

constexpr float kMarginPixels = 12.0f;
constexpr double kRateWindowS = 1.0;
constexpr float kSegmentGapPixels = 2.0f; // Between the top bar's mode buttons.
constexpr double kPi = 3.14159265358979323846;

// Node bytes are LED drive levels (gamma applied); the screen wants the frame's values back.
ImU32 to_imgui(const std::uint8_t *rgb, double gamma) {
    const auto channel = [gamma](std::uint8_t b) {
        return static_cast<int>(std::lround(255.0 * std::pow(b / 255.0, 1.0 / gamma)));
    };
    return IM_COL32(channel(rgb[0]), channel(rgb[1]), channel(rgb[2]), 255);
}

} // namespace

PreviewView::PreviewView(Session &session, ProjectState &project)
    : session_(session), project_(project), show_([&session, clock = PlayheadClock()]() mutable {
          // The show thread's playhead: the transport's position, smoothed (callbacks are
          // irregular). Files are decoded at the analysis rate, so the rate is 48 kHz times speed.
          // In live mode, the live clock (samples captured) at the capture rate.
          const double now_s =
              std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                  .count();
          if (const std::shared_ptr<const LiveInput> live = session.live_input(); live) {
              return clock.update(now_s, live->now(), live->sample_rate_hz());
          }
          const Transport &t = session.transport();
          const double rate = t.playing() ? kAnalysisSampleRateHz * t.speed() : 0.0;
          return clock.update(now_s, t.position_now(), rate);
      }) {}

void PreviewView::set_test_pattern(const TestPattern &pattern) {
    if (pattern.kind != TestPatternKind::off && !test_allowed()) {
        return;
    }
    if (pattern != config_.test) {
        config_.test = pattern;
        dirty_ = true;
    }
}

void PreviewView::set_ladder(Ladder step) {
    ladder_ = step;
    if (step >= Ladder::ambient) {
        config_.test.kind = TestPatternKind::off; // The show, not a test, goes to the lights.
    }
    config_.send_to_network = step >= Ladder::ambient;
    if (step == Ladder::ambient) {
        config_.manual_scene = ambient_scene();
    } else if (step == Ladder::react && config_.manual_scene < 0) {
        config_.manual_scene = playing_scene_;
    } else if (step == Ladder::show) {
        config_.manual_scene = -1;
    }
    dirty_ = true;
}

int PreviewView::ambient_scene() const {
    for (const char *label : {"breakdown", "intro"}) {
        for (size_t i = 0; i < config_.scenes.size(); ++i) {
            const std::vector<std::string> &s = config_.scenes[i].sections;
            if (std::find(s.begin(), s.end(), label) != s.end()) {
                return static_cast<int>(i);
            }
        }
    }
    return 0;
}

void PreviewView::start_recording() {
    if (!session_.has_track() || session_.track().track_analysis == nullptr) {
        record_status_ = "Recording needs a track with an analysis.";
        return;
    }
    sync_config(); // So the configuration recorded first is the one being shown.
    const TrackAnalysis &analysis = *session_.track().track_analysis;
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &local);
    SessionMeta meta;
    meta.app_version = PSYIGHT_VERSION;
    meta.track = session_.track().path;
    meta.source = analysis.source;
    meta.analysis_grid = analysis.grid;
    meta.pixels = static_cast<int>(config_.rig.pixels.size());
    meta.frame_rate_hz = show_.rate_hz();
    Result<std::unique_ptr<Recorder>> started =
        Recorder::start(std::filesystem::path("recordings") / stamp, meta, events_jsonl(analysis));
    if (!started) {
        record_status_ = started.error().message;
        return;
    }
    recorder_ = std::move(started.value());
    record_status_.clear();
    record_params();
    show_.set_recorder(recorder_);
}

void PreviewView::stop_recording(const std::string &why) {
    if (recorder_ == nullptr) {
        return;
    }
    show_.set_recorder(nullptr);
    record_status_ = why + " " + path_to_utf8(recorder_->folder()) + ": " +
                     std::to_string(recorder_->frames_written()) + " frames, " +
                     std::to_string(recorder_->dropped()) + " dropped.";
    recorder_.reset(); // Writes what is queued.
}

void PreviewView::record_params() {
    if (recorder_ == nullptr) {
        return;
    }
    RuntimeState runtime;
    runtime.manual_scene = config_.manual_scene;
    runtime.blackout = config_.blackout;
    runtime.hold = config_.hold;
    runtime.grid = config_.grid;
    if (config_.sections != nullptr) {
        runtime.sections = config_.sections->sections;
    }
    Result<std::string> line = params_line(config_.version, project_.project(), runtime);
    if (line) {
        recorder_->push_params(std::make_shared<const std::string>(std::move(line.value())));
    }
}

void PreviewView::draw_top_bar() {
    poll_midi();
    struct Step {
        Ladder step;
        const char *name;
        const char *meaning;
    };
    static constexpr Step kSteps[] = {
        {Ladder::preflight, "Preflight", "Checks; nothing is sent to the network."},
        {Ladder::preview, "Preview", "The engine runs for the preview (virtual node) only."},
        {Ladder::ambient, "Ambient", "Sends to the network: the ambient scene only."},
        {Ladder::react, "React", "Sends to the network: scenes chosen by hand."},
        {Ladder::show, "Show", "Sends to the network: scenes follow the sections."},
    };
    // A segmented control; the active step filled with the accent.
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(kSegmentGapPixels, ImGui::GetStyle().ItemSpacing.y));
    for (const Step &s : kSteps) {
        if (s.step != Ladder::preflight) {
            ImGui::SameLine();
        }
        if (toggle_button(s.name, ladder_ == s.step)) {
            set_ladder(s.step);
        }
        ImGui::SetItemTooltip("%s", s.meaning);
    }
    ImGui::PopStyleVar();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(6.5f * ImGui::GetFontSize());
    if (ImGui::BeginCombo("##tier", tier_name(current_tier()))) {
        for (const Tier t : {Tier::operate, Tier::tune, Tier::engineer}) {
            if (ImGui::Selectable(tier_name(t), t == current_tier())) {
                set_current_tier(t);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Depth of control (SPEC 9.1): Operate shows scenes and master "
                          "controls, Tune adds scene, layer and output settings, Engineer adds "
                          "everything else, including the flash limiter switch and the rig.");
    ImGui::SameLine();
    if (recorder_ == nullptr) {
        if (ImGui::Button("Record")) {
            start_recording();
        }
        ImGui::SetItemTooltip("Record the show to recordings/ (frames, settings, detections). "
                              "psyight_replay checks a recording replays identically.");
    } else {
        push_filled_button(theme().danger); // Recording: red, as on recorders.
        const bool stop = ImGui::Button("Stop recording");
        pop_filled_button();
        if (stop) {
            stop_recording("Recorded");
        }
        ImGui::SameLine();
        mono_text("REC %lld frames%s", static_cast<long long>(recorder_->frames_written()),
                  recorder_->dropped() > 0 ? ", some dropped" : "");
        if (!recorder_->error().empty()) {
            stop_recording(recorder_->error());
        }
    }
    if (!record_status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", record_status_.c_str());
    }
    if (!project_.project().limiter.flash_limiter) { // Persistent, at every tier (SPEC 7.7).
        ImGui::SameLine();
        ImGui::TextColored(theme().danger, "FLASH LIMITER OFF");
    }
    // Brightness, Blackout and Hold: fixed at the right, with hotkeys, no confirmation (SPEC 9.1).
    const ImGuiStyle &style = ImGui::GetStyle();
    const bool folded = !preview_open_ && !stage_;
    const float unfold = !folded ? 0.0f
                                 : ImGui::CalcTextSize(kIconUnfold).x +
                                       2.0f * style.FramePadding.x + style.ItemSpacing.x;
    ImFont *mono = theme_font(ThemeFont::mono);
    const float percent_width = mono != nullptr
                                    ? mono->CalcTextSizeA(mono->FontSize, FLT_MAX, 0.0f, "100 %").x
                                    : ImGui::CalcTextSize("100 %").x;
    const float brightness_width =
        ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + percent_width + style.ItemSpacing.x;
    const float buttons = ImGui::CalcTextSize("Blackout (B)").x +
                          ImGui::CalcTextSize("Hold (H)").x + 4.0f * style.FramePadding.x +
                          style.ItemSpacing.x + unfold + brightness_width;
    ImGui::SameLine(ImGui::GetWindowWidth() - buttons - style.WindowPadding.x);
    { // The brightness cap (Show > Limiters), as a percentage.
        double percent = 100.0 * project_.project().limiter.brightness_cap;
        set_knob_menu([&] { midi_menu("brightness"); });
        if (inline_knob("Brightness", &percent, 0.0, 100.0, "%.0f %%",
                        100.0 * LimiterParams{}.brightness_cap)) {
            project_.edit().limiter.brightness_cap = std::clamp(percent / 100.0, 0.0, 1.0);
            project_.changed();
        }
        ImGui::SameLine();
    }
    if (folded) {
        if (ImGui::Button((std::string(kIconUnfold) + "###unfold preview").c_str())) {
            preview_open_ = true;
        }
        ImGui::SetItemTooltip("Unfold the rig preview (View > Rig preview).");
        ImGui::SameLine();
    }
    const auto toggle = [this](const char *label, bool &on, const ImVec4 &colour) {
        if (toggle_button(label, on, &colour)) {
            on = !on;
            dirty_ = true;
        }
    };
    toggle("Blackout (B)", config_.blackout, theme().danger); // Safety: red, not the accent.
    ImGui::SameLine();
    toggle("Hold (H)", config_.hold, theme().accent);
    if (keys_enabled_ && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_B, false)) {
            config_.blackout = !config_.blackout;
            dirty_ = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_H, false)) {
            config_.hold = !config_.hold;
            dirty_ = true;
        }
        // 1 to 9 pick scenes, 0 returns to the sections (not in Ambient: one scene only).
        if (ladder_ != Ladder::ambient) {
            if (ImGui::IsKeyPressed(ImGuiKey_0, false)) {
                pick_scene(-1);
            }
            for (int k = 0; k < 9; ++k) {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + k), false) &&
                    k < static_cast<int>(config_.scenes.size())) {
                    pick_scene(k);
                }
            }
        }
    }
}

void PreviewView::pick_scene(int scene) {
    config_.manual_scene = scene;
    if (scene >= 0) {
        edit_scene_ = scene;
        if (ladder_ == Ladder::show) {
            ladder_ = Ladder::react; // A scene held by hand.
        }
    } else if (ladder_ == Ladder::react) {
        ladder_ = Ladder::show;
    }
    dirty_ = true;
}

void PreviewView::sync_config() {
    std::shared_ptr<const TrackAnalysis> analysis =
        session_.has_track() ? session_.track().track_analysis : nullptr;
    const Grid grid = session_.grid();
    std::shared_ptr<const LiveInput> live = session_.live_input();
    const int sample_rate_hz = live != nullptr        ? live->sample_rate_hz()
                               : session_.has_track() ? session_.track().sample_rate_hz
                                                      : 48000;
    dirty_ |= live != config_.live;
    std::shared_ptr<const SectionTimeline> sections = session_.edited_sections();
    dirty_ |= sections != config_.sections;
    dirty_ |= analysis != config_.analysis || grid.bpm != config_.grid.bpm ||
              grid.first_downbeat != config_.grid.first_downbeat ||
              sample_rate_hz != config_.sample_rate_hz || project_.version() != project_version_;
    if (!dirty_) {
        return;
    }
    if (recorder_ != nullptr && analysis != config_.analysis) {
        stop_recording("Stopped (the track or its analysis changed):");
    }
    config_.analysis = std::move(analysis);
    config_.live = std::move(live);
    config_.sections = std::move(sections);
    config_.grid = grid;
    config_.sample_rate_hz = sample_rate_hz;
    const Project &p = project_.project();
    config_.rig = project_.rig();
    config_.patch = project_.patch();
    config_.output = p.output;
    config_.limiter = p.limiter;
    config_.pixel_caps = group_caps_per_pixel(config_.rig, p.group_caps);
    config_.latency_offset_s = 1e-3 * p.latency_offset_ms;
    config_.automation = p.automation;
    config_.scenes = p.scenes;
    config_.palettes = p.palettes;
    config_.macros = p.macro_values;
    if (midi_started_ && midi_map_.learning().empty()) {
        midi_map_.bindings = p.midi; // Follows undo and reloads.
    }
    if (config_.manual_scene >= static_cast<int>(config_.scenes.size())) {
        config_.manual_scene = -1;
    }
    project_version_ = project_.version();
    config_.version = ++version_;
    show_.set_config(std::make_shared<const ShowConfig>(config_));
    dirty_ = false;
    record_params();
}

void PreviewView::draw(bool stage) {
    stage_ = stage;
    sync_config();
    ImGui::Begin("Show");
    const std::shared_ptr<const ShowFrame> frame = show_.latest();
    draw_live();
    draw_midi();
    const bool live = config_.live != nullptr;
    if (!live &&
        (!session_.has_track() || session_.track().track_analysis == nullptr || frame == nullptr)) {
        const char *message =
            session_.has_track() ? "No analysis (no tempo found)." : "No track loaded.";
        ImGui::TextUnformatted(message);
        ImGui::End();
        draw_preview(nullptr, message);
        return;
    }
    if (frame == nullptr) {
        ImGui::End();
        draw_preview(nullptr, "");
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const double since_s = std::chrono::duration<double>(now - rate_since_).count();
    if (rate_index_ < 0 || since_s >= kRateWindowS) {
        if (rate_index_ >= 0) {
            measured_fps_ = static_cast<double>(frame->index - rate_index_) / since_s;
        }
        rate_index_ = frame->index;
        rate_since_ = now;
    }
    playing_scene_ = std::max(0, frame->mix.to);

    draw_controls(*frame);
    ImGui::End();
    draw_preview(frame.get(), "");
}

void PreviewView::draw_preview(const ShowFrame *frame, const char *message) {
    if (stage_) { // Show mode: the large view where the bass roll was, no fold button.
        ImGui::Begin("Stage");
    } else if (!preview_open_) {
        return;
    } else {
        ImGui::SetNextWindowClass(&preview_window_class()); // Pinned.
        ImGui::Begin("Preview");
    }
    // The status line, with the fold button at its right.
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    if (audition_) {
        ImGui::TextColored(theme().accent, "Auditioning %s (the lights keep the show)",
                           audition_->name.c_str());
    } else if (frame != nullptr) {
        mono_text("%s%s, beat %d of the bar, %.1f A estimated%s",
                  config_.blackout ? "Blackout: "
                  : config_.hold   ? "Hold: "
                                   : "",
                  frame->section.c_str(), frame->beat_in_bar + 1, frame->report.current_a,
                  frame->report.power_scale < 1.0 ? " (power limited)" : "");
    } else {
        ImGui::TextDisabled("%s", message);
    }
    if (!stage_) {
        const std::string fold = std::string(kIconFold) + "###fold preview";
        const float fold_width =
            ImGui::CalcTextSize(kIconFold).x + 2.0f * ImGui::GetStyle().FramePadding.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - fold_width));
        if (ImGui::Button(fold.c_str())) {
            preview_open_ = false;
        }
        ImGui::SetItemTooltip("Fold the rig preview (View > Rig preview, or the button at the "
                              "top right, brings it back).");
    }
    if (frame != nullptr) {
        if (std::vector<std::uint8_t> bytes = audition_bytes(*frame); !bytes.empty()) {
            ShowFrame shown = *frame;
            shown.node_rgb = std::move(bytes);
            draw_rig(shown);
        } else {
            draw_rig(*frame);
        }
    }
    ImGui::End();
}

void PreviewView::draw_live() {
    const std::shared_ptr<const LiveInput> live = session_.live_input();
    if (!ImGui::CollapsingHeader(live != nullptr ? "Live input (on)###live"
                                                 : "Live input###live")) {
        return;
    }
    if (!session_.live_available()) {
        ImGui::TextWrapped("Live mode needs the M4 student's weights in data/m4/ (see "
                           "tools/python/m4_export.py).");
        return;
    }
    const float em = ImGui::GetFontSize();
    if (live == nullptr) {
        // Sources: 0 loopback, 1 to n input devices, -1 the loaded track as if live (silent).
        const std::string current = live_source_ == -1 ? "The loaded track, as if live (silent)"
                                    : live_source_ == 0
                                        ? "Loopback (what this computer plays)"
                                        : input_devices_[static_cast<size_t>(live_source_ - 1)];
        ImGui::SetNextItemWidth(18.0f * em);
        if (ImGui::BeginCombo("Source", current.c_str())) {
            if (ImGui::IsWindowAppearing()) {
                if (Result<std::vector<std::string>> names = Capture::input_devices(); names) {
                    input_devices_ = std::move(names.value());
                }
                live_source_ = std::min(live_source_, static_cast<int>(input_devices_.size()));
            }
            if (ImGui::Selectable("Loopback (what this computer plays)", live_source_ == 0)) {
                live_source_ = 0;
            }
            if (session_.has_track() &&
                ImGui::Selectable("The loaded track, as if live (silent)", live_source_ == -1)) {
                live_source_ = -1;
            }
            for (size_t i = 0; i < input_devices_.size(); ++i) {
                if (ImGui::Selectable(input_devices_[i].c_str(),
                                      live_source_ == static_cast<int>(i) + 1)) {
                    live_source_ = static_cast<int>(i) + 1;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Start")) {
            CaptureOptions options;
            options.source = live_source_ == 0  ? CaptureSource::loopback
                             : live_source_ < 0 ? CaptureSource::file
                                                : CaptureSource::input;
            if (live_source_ > 0) {
                options.device_name = input_devices_[static_cast<size_t>(live_source_ - 1)];
            } else if (live_source_ < 0 && session_.has_track()) {
                options.file = session_.track().path;
            }
            const Result<void> started = session_.start_live(options);
            live_error_ = started ? "" : started.error().message;
            tap_errors_s_.clear();
        }
        ImGui::SetItemTooltip("Analyses the input causally (the M4 student and a beat loop on the "
                              "kicks) and drives the show from it. Pauses the file.");
        if (!live_error_.empty()) {
            ImGui::TextColored(theme().danger, "%s", live_error_.c_str());
        }
        return;
    }
    if (ImGui::Button("Stop live input")) {
        session_.stop_live();
        return;
    }
    const LiveState s = live->state();
    const double bpm = s.tempo_known ? 60.0 / s.period_s : 0.0;
    if (!s.tempo_known) {
        ImGui::TextUnformatted("Listening for a steady kick...");
    } else {
        const double now_s = static_cast<double>(live->now()) / live->sample_rate_hz();
        const EngineInputs in = inputs_at(s, now_s);
        mono_text("%s %.2f BPM, beat %d of the bar, %s (last kick %+.1f ms off the beat)",
                  s.locked ? "Locked:" : "Acquiring:", bpm, in.beat_in_bar + 1,
                  live_section_name(s.section), s.last_error_ms);
    }
    const float level = live->level();
    mono_text("Analysis delay %.0f ms (decisions %.0f ms after the audio), load %.1f%%, "
              "level %.0f dBFS, dropped %lld samples",
              live->analysis_delay_ms(), 1e3 * s.latency_s, 100.0 * live->load(),
              level > 0.0f ? 20.0 * std::log10(level) : -120.0, live->dropped());

    // Operator controls: downbeat now, nudge, relock.
    const auto now_s = [&] { return static_cast<double>(live->now()) / live->sample_rate_hz(); };
    const bool keys = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                      !ImGui::GetIO().WantTextInput;
    ImGui::BeginDisabled(!s.tempo_known);
    if (ImGui::Button("Downbeat now (D)") || (keys && ImGui::IsKeyPressed(ImGuiKey_D, false))) {
        live->command({LiveCommand::Kind::downbeat, now_s()});
    }
    ImGui::SetItemTooltip("Press on the \"one\" you hear: the nearest beat becomes the downbeat.");
    ImGui::SameLine();
    if (ImGui::Button("-10 ms")) {
        live->command({LiveCommand::Kind::nudge, -0.010});
    }
    ImGui::SameLine();
    if (ImGui::Button("+10 ms")) {
        live->command({LiveCommand::Kind::nudge, 0.010});
    }
    ImGui::SetItemTooltip("Nudges the predicted beat; the loop then pulls it back towards the "
                          "kicks it hears.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Relock")) {
        live->command({LiveCommand::Kind::relock});
    }
    ImGui::SetItemTooltip("Measures the tempo again from the next kicks.");

    // Tap test: taps against the predicted beat, on the live clock.
    if (ImGui::Button("Tap (T)") ||
        (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
         ImGui::IsKeyPressed(ImGuiKey_T, false))) {
        tap();
    }
    ImGui::SetItemTooltip("Tap on the beat you hear, at least 8 times. Measures when you hear the "
                          "beat against the capture clock (speaker distance, the sound system). "
                          "The GUI reads taps once a frame, so allow about 5 ms of scatter.");
    ImGui::SameLine();
    if (ImGui::Button("Clear taps")) {
        tap_errors_s_.clear();
    }
    if (tap_errors_s_.size() >= 2) {
        double mean = 0.0;
        for (const double e : tap_errors_s_) {
            mean += e;
        }
        mean /= static_cast<double>(tap_errors_s_.size());
        double var = 0.0;
        for (const double e : tap_errors_s_) {
            var += (e - mean) * (e - mean);
        }
        const double sd = std::sqrt(var / static_cast<double>(tap_errors_s_.size() - 1));
        mono_text("%zu taps: %+.0f ms after the predicted beat (sd %.0f ms)", tap_errors_s_.size(),
                  1e3 * mean, 1e3 * sd);
        ImGui::BeginDisabled(tap_errors_s_.size() < 8);
        if (ImGui::Button("Use as latency offset")) {
            // Lights show beat b when the render time reaches b; it is heard at b + mean on the
            // capture clock, so render mean later: offset = -mean.
            project_.edit().latency_offset_ms = std::clamp(-1e3 * mean, -200.0, 200.0);
            project_.changed();
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Sets the latency offset so the lights land when the beat is heard. "
                              "The LED path (network, node) adds its own delay: check by eye.");
    }
}

void PreviewView::tap() {
    const std::shared_ptr<const LiveInput> live = session_.live_input();
    if (live == nullptr) {
        return;
    }
    const LiveState s = live->state();
    if (s.tempo_known) {
        const double now_s = static_cast<double>(live->now()) / live->sample_rate_hz();
        tap_errors_s_.push_back(std::remainder(LiveBeat::position(s, now_s), 1.0) * s.period_s);
    }
}

void PreviewView::poll_midi() {
    if (!midi_started_) {
        midi_started_ = true;
        midi_map_.bindings = project_.project().midi;
        if (const std::string &port = project_.project().midi_port; !port.empty()) {
            if (const Result<void> opened = midi_in_.open(port); !opened) {
                midi_error_ = opened.error().message;
            }
        }
    }
    if (!midi_in_.is_open()) {
        return;
    }
    midi_messages_.clear();
    midi_in_.drain(midi_messages_);
    for (const MidiMessage &m : midi_messages_) {
        const bool was_learning = !midi_map_.learning().empty();
        for (const MidiAction &action : midi_map_.handle(m)) {
            apply_midi(action);
        }
        if (was_learning && midi_map_.learning().empty()) {
            project_.edit().midi = midi_map_.bindings;
            project_.changed();
        }
    }
}

void PreviewView::apply_midi(const MidiAction &a) {
    if (a.target == "blackout") {
        config_.blackout = !config_.blackout;
        dirty_ = true;
    } else if (a.target == "hold") {
        config_.hold = !config_.hold;
        dirty_ = true;
    } else if (a.target == "auto") {
        config_.manual_scene = -1;
        dirty_ = true;
    } else if (a.target.rfind("scene:", 0) == 0) {
        const int scene = std::atoi(a.target.c_str() + 6);
        if (scene >= 0 && scene < static_cast<int>(config_.scenes.size())) {
            config_.manual_scene = scene;
            dirty_ = true;
        }
    } else if (a.target == "brightness") {
        project_.edit().limiter.brightness_cap = std::clamp(a.value, 0.0, 1.0);
        project_.changed();
    } else if (a.target == "latency") {
        project_.edit().latency_offset_ms = -200.0 + 400.0 * std::clamp(a.value, 0.0, 1.0);
        project_.changed();
    } else if (a.target.rfind("macro:", 0) == 0) {
        const int macro = std::atoi(a.target.c_str() + 6);
        if (macro >= 0 && macro < kMacroCount) {
            project_.edit().macro_values[static_cast<size_t>(macro)] =
                std::clamp(a.value, 0.0, 1.0);
            project_.changed();
        }
    } else if (a.target == "tap") {
        tap();
    } else if (a.target == "downbeat") {
        if (const std::shared_ptr<const LiveInput> live = session_.live_input(); live) {
            live->command({LiveCommand::Kind::downbeat,
                           static_cast<double>(live->now()) / live->sample_rate_hz()});
        }
    }
}

void PreviewView::draw_midi() {
    if (!ImGui::CollapsingHeader(midi_in_.is_open() ? "MIDI (on)###midi" : "MIDI###midi")) {
        return;
    }
    const float em = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(16.0f * em);
    const char *shown = midi_in_.is_open() ? midi_in_.port().c_str() : "(none)";
    if (ImGui::BeginCombo("Controller", shown)) {
        if (ImGui::IsWindowAppearing()) {
            if (Result<std::vector<std::string>> ports = MidiInput::ports(); ports) {
                midi_ports_ = std::move(ports.value());
            } else {
                midi_error_ = ports.error().message;
            }
        }
        if (ImGui::Selectable("(none)", !midi_in_.is_open())) {
            midi_in_.close();
            project_.edit().midi_port.clear();
            project_.changed();
        }
        for (const std::string &port : midi_ports_) {
            if (ImGui::Selectable(port.c_str(), midi_in_.port() == port)) {
                const Result<void> opened = midi_in_.open(port);
                midi_error_ = opened ? "" : opened.error().message;
                if (opened) {
                    project_.edit().midi_port = port;
                    project_.changed();
                }
            }
        }
        ImGui::EndCombo();
    }
    if (!midi_error_.empty()) {
        ImGui::TextColored(theme().danger, "%s", midi_error_.c_str());
    }
    std::vector<std::pair<std::string, std::string>> targets = {
        {"blackout", "Blackout"},           {"hold", "Hold"},
        {"auto", "Automation (scenes)"},    {"brightness", "Brightness cap"},
        {"latency", "Latency offset"},      {"tap", "Tap test"},
        {"downbeat", "Downbeat now (live)"}};
    for (int i = 0; i < kMacroCount; ++i) {
        targets.emplace_back("macro:" + std::to_string(i), macro_name(project_.project(), i));
    }
    for (size_t i = 0; i < config_.scenes.size(); ++i) {
        targets.emplace_back("scene:" + std::to_string(i), "Scene: " + config_.scenes[i].name);
    }
    if (!ImGui::BeginTable("##midi", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        return;
    }
    for (const auto &[target, label] : targets) {
        ImGui::PushID(target.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(label.c_str());
        ImGui::TableNextColumn();
        const MidiBinding *b = midi_map_.binding_of(target);
        if (midi_map_.learning() == target) {
            ImGui::TextColored(theme().accent, "move a control...");
        } else if (b != nullptr) {
            ImGui::TextUnformatted(describe(*b).c_str());
        } else {
            ImGui::TextDisabled("none");
        }
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!midi_in_.is_open());
        if (ImGui::SmallButton(midi_map_.learning() == target ? "Cancel" : "Learn")) {
            if (midi_map_.learning() == target) {
                midi_map_.cancel_learn();
            } else {
                midi_map_.learn(target);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(b == nullptr);
        if (ImGui::SmallButton("Clear")) {
            midi_map_.clear(target);
            project_.edit().midi = midi_map_.bindings;
            project_.changed();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void PreviewView::draw_controls(const ShowFrame &frame) {
    const float em = ImGui::GetFontSize();
    draw_scene_cards(frame);
    ImGui::SetNextItemWidth(7.0f * em);
    float latency_ms = static_cast<float>(project_.project().latency_offset_ms);
    if (mono_field("Latency offset", [&](const char *id) {
            return ImGui::DragFloat(id, &latency_ms, 1.0f, -200.0f, 200.0f, "%+.0f ms");
        })) {
        project_.edit().latency_offset_ms = latency_ms;
        project_.changed();
    }
    ImGui::SetItemTooltip("Render time minus playhead. About 2.9 ms per metre from the speakers "
                          "(SPEC 4.3).");
    draw_macros();
    if (tier_at_least(Tier::tune)) {
        draw_library();
        std::vector<Scene> &scenes = project_.edit().scenes;
        edit_scene_ = std::clamp(edit_scene_, 0, static_cast<int>(scenes.size()) - 1);
        const std::string scene_label =
            "Scene: " + scenes[static_cast<size_t>(edit_scene_)].name + "###scene";
        if (ImGui::TreeNode(scene_label.c_str())) {
            draw_scene_editor(scenes, frame);
            ImGui::TreePop();
        }
        apply_pending_scene_preset();
        if (scene_edited_) {
            project_.changed();
            scene_edited_ = false;
        }
        draw_palettes();
    }
    if (ImGui::TreeNode("Limiters")) {
        LimiterParams p = project_.project().limiter;
        bool changed = false;
        const LimiterParams defaults;
        set_knob_menu([&] { midi_menu("brightness"); });
        changed |=
            knob("Brightness##cap", &p.brightness_cap, 0.0, 1.0, "%.2f", defaults.brightness_cap);
        ImGui::SetItemTooltip("Brightness cap: every pixel at most this.");
        if (tier_at_least(Tier::tune)) {
            ImGui::SameLine();
            changed |= knob("Power##budget", &p.budget_a, 0.0, 100.0,
                            p.budget_a <= 0.0 ? "off" : "%.1f A", defaults.budget_a);
            ImGui::SetItemTooltip("Power budget: the frame is scaled to stay under it (0: off).");
            draw_group_caps();
        }
        if (tier_at_least(Tier::engineer)) { // Disabling it is an Engineer action (SPEC 7.7).
            changed |= ImGui::Checkbox("Flash limiter (3 per second)", &p.flash_limiter);
        } else {
            ImGui::TextDisabled("Flash limiter %s (its switch is in the Engineer tier).",
                                p.flash_limiter ? "on" : "off");
        }
        if (changed) {
            project_.edit().limiter = p;
            project_.changed();
        }
        if (!p.flash_limiter) {
            ImGui::TextColored(theme().danger, "Flash limiter off: photosensitive epilepsy risk "
                                               "(SPEC 7.7).");
        }
        mono_text("Flashes held back so far: %lld", static_cast<long long>(frame.flashes_limited));
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Output")) {
        draw_output(frame);
        ImGui::TreePop();
    }
}

void PreviewView::draw_scene_editor(std::vector<Scene> &scenes, const ShowFrame &frame) {
    const float em = ImGui::GetFontSize();
    draw_scene_presets();
    Scene &scene = scenes[static_cast<size_t>(edit_scene_)];
    ImGui::SetNextItemWidth(10.0f * em);
    scene_edited_ |= ImGui::InputText("Name", &scene.name);
    const std::vector<Palette> &palettes = project_.project().palettes;
    scene.palette = std::clamp(scene.palette, 0, static_cast<int>(palettes.size()) - 1);
    ImGui::SetNextItemWidth(9.0f * em);
    if (ImGui::BeginCombo("Palette (base)",
                          palettes[static_cast<size_t>(scene.palette)].name.c_str())) {
        for (int i = 0; i < static_cast<int>(palettes.size()); ++i) {
            if (ImGui::Selectable(palettes[static_cast<size_t>(i)].name.c_str(),
                                  i == scene.palette)) {
                scene.palette = i;
                scene_edited_ = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Automation shifts it by one every %d phrases.",
                          project_.project().automation.palette_every_phrases);
    // Section tags: which sections the automation may pick this scene for; wraps like the cards.
    ImGui::TextUnformatted("For");
    const ImGuiStyle &style = ImGui::GetStyle();
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    for (const char *label : {"intro", "groove", "breakdown", "build", "drop", "outro"}) {
        const float width =
            ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
        if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width < right) {
            ImGui::SameLine();
        }
        auto it = std::find(scene.sections.begin(), scene.sections.end(), label);
        bool tagged = it != scene.sections.end();
        if (ImGui::Checkbox(label, &tagged)) {
            if (tagged) {
                scene.sections.emplace_back(label);
            } else {
                scene.sections.erase(it);
            }
            scene_edited_ = true;
        }
    }
    // Energy tags: preferred for sections of that energy (none: any energy).
    ImGui::TextUnformatted("Energy");
    for (const char *label : kEnergyTags) {
        ImGui::SameLine();
        auto it = std::find(scene.sections.begin(), scene.sections.end(), label);
        bool tagged = it != scene.sections.end();
        if (ImGui::Checkbox((std::string(label) + "##energy").c_str(), &tagged)) {
            if (tagged) {
                scene.sections.emplace_back(label);
            } else {
                scene.sections.erase(it);
            }
            scene_edited_ = true;
        }
        ImGui::SetItemTooltip("Preferred for %s-energy sections; with no energy ticked, the scene "
                              "suits any energy.",
                              label);
    }

    draw_rack(scene, frame);
    draw_routes(scene, frame);

    // The scene list.
    if (ImGui::Button("Duplicate scene")) {
        Scene copy = scene;
        copy.name += " (copy)";
        scenes.insert(scenes.begin() + edit_scene_ + 1, std::move(copy));
        ++edit_scene_;
        scene_edited_ = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(scenes.size() < 2);
    if (ImGui::Button("Delete scene")) {
        scenes.erase(scenes.begin() + edit_scene_);
        edit_scene_ = std::max(0, edit_scene_ - 1);
        config_.manual_scene = -1;
        dirty_ = true;
        scene_edited_ = true;
    }
    ImGui::EndDisabled();
}

void PreviewView::draw_group_caps() {
    // One cap per group tag in the rig (SPEC 7.7: global, and per group).
    std::vector<std::string> groups;
    for (const ProjectStrip &strip : project_.project().strips) {
        for (const std::string &g : strip.spec.groups) {
            if (std::find(groups.begin(), groups.end(), g) == groups.end()) {
                groups.push_back(g);
            }
        }
    }
    if (groups.empty()) {
        return;
    }
    // One knob per group, after the global ones (the lowest cap wins).
    for (const std::string &g : groups) {
        std::vector<GroupCap> &caps = project_.edit().group_caps;
        auto it = std::find_if(caps.begin(), caps.end(),
                               [&g](const GroupCap &c) { return c.group == g; });
        double cap = it == caps.end() ? 1.0 : it->cap;
        ImGui::SameLine();
        const bool turned = knob(g.c_str(), &cap, 0.0, 1.0, "%.2f", 1.0);
        ImGui::SetItemTooltip("Cap for the strips tagged %s (the lowest cap wins).", g.c_str());
        if (turned) {
            if (it == caps.end()) {
                caps.push_back({g, cap});
            } else {
                it->cap = cap;
            }
            project_.changed();
        }
    }
}

void PreviewView::draw_palettes() {
    if (!ImGui::TreeNode("Palettes")) {
        return;
    }
    draw_palette_library();
    Project &project = project_.edit();
    std::vector<Palette> &palettes = project.palettes;
    bool changed = false;
    int remove = -1;
    for (size_t p = 0; p < palettes.size(); ++p) {
        Palette &palette = palettes[p];
        ImGui::PushID(static_cast<int>(p));
        ImGui::SetNextItemWidth(7.0f * ImGui::GetFontSize());
        changed |= ImGui::InputText("##name", &palette.name);
        int remove_colour = -1;
        for (size_t c = 0; c < palette.colours.size(); ++c) {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(c));
            float rgb[3] = {palette.colours[c].r, palette.colours[c].g, palette.colours[c].b};
            if (ImGui::ColorEdit3("##colour", rgb, ImGuiColorEditFlags_NoInputs)) {
                palette.colours[c] = {rgb[0], rgb[1], rgb[2]};
                changed = true;
            }
            if (ImGui::BeginPopupContextItem("colour menu")) {
                if (ImGui::MenuItem("Remove colour", nullptr, false, palette.colours.size() > 1)) {
                    remove_colour = static_cast<int>(c);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (remove_colour >= 0) {
            palette.colours.erase(palette.colours.begin() + remove_colour);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) {
            palette.colours.push_back(palette.colours.back());
            changed = true;
        }
        ImGui::SetItemTooltip("Add a colour. Right-click a colour to remove it.");
        ImGui::SameLine();
        ImGui::BeginDisabled(palettes.size() < 2);
        if (ImGui::SmallButton("x")) {
            remove = static_cast<int>(p);
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Delete this palette (scenes using it move to the first).");
        ImGui::PopID();
    }
    if (remove >= 0) {
        palettes.erase(palettes.begin() + remove);
        for (Scene &scene : project.scenes) {
            if (scene.palette == remove) {
                scene.palette = 0;
            } else if (scene.palette > remove) {
                --scene.palette;
            }
        }
        changed = true;
    }
    if (ImGui::Button("Add palette")) {
        palettes.push_back({"Palette " + std::to_string(palettes.size() + 1),
                            {{0.5f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.6f}}});
        changed = true;
    }
    ImGui::TextDisabled("Keep white out: it is for kicks and drops (SPEC 7.5).");
    if (changed) {
        project_.changed();
    }
    ImGui::TreePop();
}

void PreviewView::draw_routes(Scene &scene, const ShowFrame &frame) {
    ImGui::SeparatorText("Modulation");
    const ImGuiStyle &style = ImGui::GetStyle();
    const float row = ImGui::GetFrameHeight() + style.ItemSpacing.y;
    const float width = 2.0f * device_padding() + 3.0f * knob_width() + 2.0f * style.ItemSpacing.x;
    // An empty rack is just the add button.
    const float height = scene.routes.empty() ? ImGui::GetFrameHeight() + 2.0f * device_padding()
                                              : device_title_height() + 4.0f * row + knob_height() +
                                                    device_padding();
    const bool live =
        frame.mix.to == edit_scene_ && frame.route_values.size() == scene.routes.size();
    int remove = -1;
    ImGui::BeginChild("##routes", ImVec2(0.0f, height + style.ScrollbarSize + style.ItemSpacing.y),
                      ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (size_t i = 0; i < scene.routes.size(); ++i) {
        ModRoute &r = scene.routes[i];
        if (i > 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(static_cast<int>(i));
        begin_device("##route", ImVec2(width, height));
        const float full = ImGui::GetContentRegionAvail().x;
        // Title: the source, and remove.
        push_device_title();
        const float remove_width =
            ImGui::CalcTextSize(kIconRemove).x + 2.0f * ImGui::GetStyle().FramePadding.x;
        ImGui::SetNextItemWidth(full - remove_width - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::BeginCombo("##source", source_label(r.source).c_str())) {
            for (const ModSource s : kModSources) {
                if (ImGui::Selectable(source_label(s).c_str(), s == r.source)) {
                    r.source = s;
                    scene_edited_ = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Source");
        ImGui::SameLine();
        if (device_icon_button(kIconRemove, "remove", "Remove this route.")) {
            remove = static_cast<int>(i);
        }
        pop_device_title();
        ImGui::SetCursorPosY(device_title_height());
        // Curve and invert.
        const float invert_width = ImGui::CalcTextSize("Inv").x + 2.0f * style.FramePadding.x;
        ImGui::SetNextItemWidth(full - invert_width - style.ItemSpacing.x);
        if (ImGui::BeginCombo("##curve", mod_curve_name(r.curve))) {
            for (const ModCurve c : kModCurves) {
                if (ImGui::Selectable(mod_curve_name(c), c == r.curve)) {
                    r.curve = c;
                    scene_edited_ = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Curve");
        ImGui::SameLine();
        if (toggle_button("Inv###invert", r.invert)) {
            r.invert = !r.invert;
            scene_edited_ = true;
        }
        ImGui::SetItemTooltip("Invert: 1 minus the shaped source.");
        // Target (and for a setting, which one), and its layer.
        const bool setting = r.target == ModTarget::setting && !scene.layers.empty();
        const float half = 0.5f * (full - style.ItemSpacing.x);
        ImGui::SetNextItemWidth(setting ? half : full);
        if (ImGui::BeginCombo("##target", mod_target_name(r.target))) {
            for (const ModTarget t : kModTargets) {
                if (ImGui::Selectable(mod_target_name(t), t == r.target)) {
                    r.target = t;
                    r.amount = t == ModTarget::hue_shift ? 60.0 : 0.5;
                    scene_edited_ = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Target");
        if (r.target == ModTarget::setting && !scene.layers.empty()) {
            // A setting of the route's layer: keep the id valid for that layer's effect.
            r.layer = std::clamp(r.layer, 0, static_cast<int>(scene.layers.size()) - 1);
            const std::vector<EffectParamInfo> infos =
                effect_param_infos(scene.layers[static_cast<size_t>(r.layer)].spec.type());
            const auto found = std::find_if(infos.begin(), infos.end(),
                                            [&](const auto &info) { return r.param == info.id; });
            if (found == infos.end() && !infos.empty()) {
                r.param = infos.front().id;
                scene_edited_ = true;
            }
            const char *shown = found != infos.end() ? found->label : "";
            ImGui::SameLine();
            ImGui::SetNextItemWidth(half);
            if (ImGui::BeginCombo("##setting", shown)) {
                for (const EffectParamInfo &info : infos) {
                    if (ImGui::Selectable(info.label, r.param == info.id)) {
                        r.param = info.id;
                        scene_edited_ = true;
                    }
                    ImGui::SetItemTooltip("%s", info.description);
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("The layer's setting this route moves (amount: a fraction of "
                                  "its range).");
        }
        ImGui::SetNextItemWidth(full);
        if (r.target == ModTarget::brightness || scene.layers.empty()) {
            ImGui::BeginDisabled();
            if (ImGui::BeginCombo("##layer", "Whole scene")) {
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        } else {
            r.layer = std::clamp(r.layer, 0, static_cast<int>(scene.layers.size()) - 1);
            if (ImGui::BeginCombo("##layer",
                                  scene.layers[static_cast<size_t>(r.layer)].effect->name())) {
                for (int k = 0; k < static_cast<int>(scene.layers.size()); ++k) {
                    if (ImGui::Selectable(scene.layers[static_cast<size_t>(k)].effect->name(),
                                          k == r.layer)) {
                        r.layer = k;
                        scene_edited_ = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Layer");
        }
        // Hold per grid division.
        ImGui::SetNextItemWidth(full);
        if (ImGui::BeginCombo("##quantise", mod_quantise_name(r.quantise))) {
            for (const ModQuantise q : kModQuantises) {
                if (ImGui::Selectable(mod_quantise_name(q), q == r.quantise)) {
                    r.quantise = q;
                    scene_edited_ = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Free follows the source; Hold samples it at each 16th, beat or bar "
                              "and keeps it until the next.");
        // Amount (its ring: what the route adds right now), attack and release.
        const bool hue = r.target == ModTarget::hue_shift;
        const double now = live ? r.amount * frame.route_values[i] : 0.0;
        if (knob("Amount", &r.amount, hue ? -180.0 : -1.0, hue ? 180.0 : 1.0,
                 hue ? "%+.0f deg" : "%+.2f", hue ? 60.0 : 0.5, live ? &now : nullptr)) {
            scene_edited_ = true;
        }
        ImGui::SameLine();
        double attack_ms = 1e3 * r.attack_s;
        if (knob("Attack", &attack_ms, 0.0, 1e3 * kMaxSmoothingS, "%.0f ms", 0.0)) {
            r.attack_s = 1e-3 * attack_ms;
            scene_edited_ = true;
        }
        ImGui::SameLine();
        double release_ms = 1e3 * r.release_s;
        if (knob("Release", &release_ms, 0.0, 1e3 * kMaxSmoothingS, "%.0f ms", 0.0)) {
            r.release_s = 1e-3 * release_ms;
            scene_edited_ = true;
        }
        end_device();
        ImGui::PopID();
    }
    if (!scene.routes.empty()) {
        ImGui::SameLine();
    }
    begin_device("##add route", ImVec2(knob_width(), height));
    ImGui::SetCursorPosY(0.5f * (height - ImGui::GetFrameHeight()));
    if (device_icon_button(kIconAdd, "add route",
                           "Add a route: source, curve and amount onto a layer's opacity or hue, "
                           "or the scene's brightness (SPEC 7.4). Routes to one target add up.")) {
        scene.routes.push_back({});
        scene_edited_ = true;
    }
    end_device();
    ImGui::EndChild();
    if (remove >= 0) {
        scene.routes.erase(scene.routes.begin() + remove);
        scene_edited_ = true;
    }
}

void PreviewView::draw_rack(Scene &scene, const ShowFrame &frame) {
    ImGui::SeparatorText("Layers");
    const ImGuiStyle &style = ImGui::GetStyle();
    const float height = device_title_height() + ImGui::GetFrameHeight() + style.ItemSpacing.y +
                         2.0f * knob_height() + style.ItemSpacing.y + device_padding();
    const bool live =
        frame.mix.to == edit_scene_ && frame.route_values.size() == scene.routes.size();
    int remove = -1;
    int move_from = -1; // A layer dragged (or moved from the menu) to move_to.
    int move_to = -1;
    ImGui::BeginChild("##rack", ImVec2(0.0f, height + style.ScrollbarSize + style.ItemSpacing.y),
                      ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (size_t i = 0; i < scene.layers.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        draw_layer_device(scene, i, live ? &frame.route_values : nullptr, height, remove, move_from,
                          move_to);
    }
    if (!scene.layers.empty()) {
        ImGui::SameLine();
    }
    begin_device("##add layer", ImVec2(knob_width(), height));
    ImGui::SetCursorPosY(0.5f * (height - ImGui::GetFrameHeight()));
    if (device_icon_button(kIconAdd, "add layer", "Add a layer (on top, at the right).")) {
        ImGui::OpenPopup("##add layer menu");
    }
    if (ImGui::BeginPopup("##add layer menu")) {
        for (const EffectType t : kEffectTypes) {
            const Layer layer = make_layer(default_spec(t), default_blend(t));
            if (ImGui::Selectable(layer.effect->name())) {
                scene.layers.push_back(layer);
                scene_edited_ = true;
            }
        }
        ImGui::EndPopup();
    }
    end_device();
    ImGui::EndChild();
    if (move_from >= 0 && move_to >= 0 && move_from != move_to) {
        // Adjacent swaps keep every route on its layer.
        for (int k = move_from; k != move_to; k += move_to > move_from ? 1 : -1) {
            const int next = move_to > move_from ? k + 1 : k - 1;
            swap_layers(scene, static_cast<size_t>(std::min(k, next)),
                        static_cast<size_t>(std::max(k, next)));
        }
        scene_edited_ = true;
    }
    if (remove >= 0) {
        remove_layer(scene, static_cast<size_t>(remove));
        scene_edited_ = true;
    }
    // Knob menus: routes removed (highest index first) and added.
    std::sort(routes_to_remove_.rbegin(), routes_to_remove_.rend());
    for (const int r : routes_to_remove_) {
        if (r >= 0 && r < static_cast<int>(scene.routes.size())) {
            scene.routes.erase(scene.routes.begin() + r);
            scene_edited_ = true;
        }
    }
    routes_to_remove_.clear();
    for (const ModRoute &r : routes_to_add_) {
        scene.routes.push_back(r);
        scene_edited_ = true;
    }
    routes_to_add_.clear();
}

void PreviewView::modulation_menu(const Scene &scene, int layer, ModTarget target,
                                  const char *param, double value, double min, double max) {
    ImGui::Separator();
    const auto made = [&](ModSource source, double amount) {
        ModRoute r;
        r.source = source;
        r.target = target;
        r.layer = layer;
        r.param = param;
        r.amount = amount;
        return r;
    };
    if (ImGui::BeginMenu("Modulate with")) {
        for (const ModSource s : kModSources) {
            if (ImGui::MenuItem(source_label(s).c_str())) {
                routes_to_add_.push_back(made(s, target == ModTarget::setting ? 0.25 : 0.5));
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Map to macro")) {
        // The macro sweeps from the knob's value (macro at 0) to its top (at 1); a knob already
        // at the top sweeps down to the bottom instead.
        const double amount = macro_mapping_amount(value, min, max);
        for (int i = 0; i < kMacroCount; ++i) {
            if (ImGui::MenuItem(macro_name(project_.project(), i).c_str())) {
                routes_to_add_.push_back(
                    made(static_cast<ModSource>(static_cast<int>(ModSource::macro_1) + i), amount));
            }
        }
        ImGui::TextDisabled("The macro sweeps this knob from here to the top.");
        ImGui::EndMenu();
    }
    std::vector<int> here;
    for (size_t r = 0; r < scene.routes.size(); ++r) {
        const ModRoute &route = scene.routes[r];
        if (route.layer == layer && route.target == target &&
            (target != ModTarget::setting || route.param == param)) {
            here.push_back(static_cast<int>(r));
        }
    }
    if (!here.empty() && ImGui::BeginMenu("Remove modulation")) {
        for (const int r : here) {
            const ModRoute &route = scene.routes[static_cast<size_t>(r)];
            char label[96];
            std::snprintf(label, sizeof(label), "%s, %+.2f##%d", source_label(route.source).c_str(),
                          route.amount, r);
            if (ImGui::MenuItem(label)) {
                routes_to_remove_.push_back(r);
            }
        }
        ImGui::EndMenu();
    }
}

void PreviewView::midi_menu(const std::string &target) {
    ImGui::Separator();
    const bool learning = midi_map_.learning() == target;
    if (ImGui::MenuItem(learning ? "Cancel MIDI learn" : "Learn MIDI")) {
        if (learning) {
            midi_map_.cancel_learn();
        } else {
            midi_map_.learn(target);
        }
    }
    if (const MidiBinding *bound = midi_map_.binding_of(target); bound != nullptr) {
        if (ImGui::MenuItem(("Forget " + describe(*bound)).c_str())) {
            midi_map_.clear(target);
            project_.edit().midi = midi_map_.bindings;
            project_.changed();
        }
    }
    if (!midi_in_.is_open()) {
        ImGui::TextDisabled("No MIDI controller open (MIDI section).");
    } else if (learning) {
        ImGui::TextDisabled("Move a control on the controller.");
    }
}

void PreviewView::draw_layer_device(Scene &scene, size_t index,
                                    const std::vector<double> *route_values, float height,
                                    int &remove, int &move_from, int &move_to) {
    Layer &layer = scene.layers[index];
    const std::vector<EffectParamInfo> infos = effect_param_infos(layer.spec.type());
    const EffectSpec defaults = default_spec(layer.spec.type());
    const int knobs = 1 + static_cast<int>(infos.size());
    const int columns = std::max(3, (knobs + 1) / 2);
    const ImGuiStyle &style = ImGui::GetStyle();
    const float width = 2.0f * device_padding() + columns * knob_width() +
                        static_cast<float>(columns - 1) * style.ItemSpacing.x;
    ImGui::PushID(static_cast<int>(index));
    begin_device("##device", ImVec2(width, height));
    const float full = ImGui::GetContentRegionAvail().x;

    // Title: power (the accent when on), the name (drag to reorder; right-click: move or
    // remove) and remove.
    push_device_title();
    ImGui::PushStyleColor(ImGuiCol_Text, layer.enabled ? theme().accent : theme().text_dim);
    if (device_icon_button(kIconPower, "on",
                           layer.enabled ? "On. Click to switch the layer off."
                                         : "Off. Click to switch the layer on.")) {
        layer.enabled = !layer.enabled;
        scene_edited_ = true;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    const float icon_button = ImGui::CalcTextSize(kIconRemove).x + 2.0f * style.FramePadding.x;
    const float name_width = std::max(1.0f, full - ImGui::GetItemRectSize().x - icon_button -
                                                2.0f * style.ItemSpacing.x);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##name", ImVec2(name_width, ImGui::GetFrameHeight()));
    if (ImGui::BeginDragDropSource()) {
        const int dragged = static_cast<int>(index);
        ImGui::SetDragDropPayload("psyight layer", &dragged, sizeof(dragged));
        ImGui::TextUnformatted(layer.effect->name());
        ImGui::EndDragDropSource();
    }
    ImGui::SetItemTooltip("%s. Drag to reorder; right-click to move or remove.",
                          layer.effect->name());
    if (ImGui::BeginPopupContextItem("##layer menu")) {
        if (ImGui::MenuItem("Move earlier", nullptr, false, index > 0)) {
            move_from = static_cast<int>(index);
            move_to = static_cast<int>(index) - 1;
        }
        if (ImGui::MenuItem("Move later", nullptr, false, index + 1 < scene.layers.size())) {
            move_from = static_cast<int>(index);
            move_to = static_cast<int>(index) + 1;
        }
        if (ImGui::MenuItem("Remove")) {
            remove = static_cast<int>(index);
        }
        ImGui::EndPopup();
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImFont *title_font = theme_font(ThemeFont::ui_medium);
    draw->PushClipRect(at, ImVec2(at.x + name_width, at.y + ImGui::GetFrameHeight()), true);
    draw->AddText(title_font, ImGui::GetFontSize(), ImVec2(at.x, at.y + style.FramePadding.y),
                  to_u32(layer.enabled ? theme().text : theme().text_dim), layer.effect->name());
    draw->PopClipRect();
    ImGui::SameLine();
    if (device_icon_button(kIconRemove, "remove", "Remove this layer and its routes.")) {
        remove = static_cast<int>(index);
    }
    pop_device_title();

    // Blend mode and group mask.
    ImGui::SetCursorPosY(device_title_height());
    const float half = 0.5f * (full - style.ItemSpacing.x);
    ImGui::SetNextItemWidth(half);
    if (ImGui::BeginCombo("##blend", blend_mode_id(layer.blend))) {
        for (const BlendMode m : kBlendModes) {
            if (ImGui::Selectable(blend_mode_id(m), m == layer.blend)) {
                layer.blend = m;
                scene_edited_ = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Blend mode");
    ImGui::SameLine();
    std::vector<std::string> groups;
    for (const ProjectStrip &strip : project_.project().strips) {
        for (const std::string &g : strip.spec.groups) {
            if (std::find(groups.begin(), groups.end(), g) == groups.end()) {
                groups.push_back(g);
            }
        }
    }
    ImGui::SetNextItemWidth(half);
    if (ImGui::BeginCombo("##strips", layer.group.empty() ? "All strips" : layer.group.c_str())) {
        if (ImGui::Selectable("All strips", layer.group.empty())) {
            layer.group.clear();
            scene_edited_ = true;
        }
        for (const std::string &g : groups) {
            if (ImGui::Selectable(g.c_str(), g == layer.group)) {
                layer.group = g;
                scene_edited_ = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Group mask: only the strips tagged with this group (Rig tab).");

    // Knobs: opacity, then the effect's parameters.
    double opacity = layer.opacity;
    double modulated = opacity;
    std::string routes_here;
    for (size_t r = 0; r < scene.routes.size(); ++r) {
        const ModRoute &route = scene.routes[r];
        if (route.layer != static_cast<int>(index) || route.target != ModTarget::opacity) {
            continue;
        }
        if (route_values != nullptr) {
            modulated += route.amount * (*route_values)[r];
        }
        char line[96];
        std::snprintf(line, sizeof(line), "\n%s, %+.2f", source_label(route.source).c_str(),
                      route.amount);
        routes_here += line;
    }
    modulated = std::clamp(modulated, 0.0, 1.0);
    set_knob_menu([&] {
        modulation_menu(scene, static_cast<int>(index), ModTarget::opacity, "", opacity, 0.0, 1.0);
    });
    if (knob("Opacity", &opacity, 0.0, 1.0, "%.2f", 1.0,
             route_values != nullptr && !routes_here.empty() ? &modulated : nullptr)) {
        layer.opacity = static_cast<float>(opacity);
        scene_edited_ = true;
    }
    if (!routes_here.empty()) {
        ImGui::SetItemTooltip("Opacity, modulated by:%s", routes_here.c_str());
    }
    // Routes moving each setting: their live sum (a fraction of the range each) and a list for
    // the tooltip.
    std::vector<double> setting_offsets(infos.size(), 0.0);
    std::vector<std::string> setting_routes(infos.size());
    for (size_t r = 0; r < scene.routes.size(); ++r) {
        const ModRoute &route = scene.routes[r];
        if (route.layer != static_cast<int>(index) || route.target != ModTarget::setting) {
            continue;
        }
        for (size_t k = 0; k < infos.size(); ++k) {
            if (route.param != infos[k].id) {
                continue;
            }
            if (route_values != nullptr) {
                setting_offsets[k] +=
                    route.amount * (infos[k].max - infos[k].min) * (*route_values)[r];
            }
            char line[96];
            std::snprintf(line, sizeof(line), "\n%s, %+.2f", source_label(route.source).c_str(),
                          route.amount);
            setting_routes[k] += line;
        }
    }
    bool params_changed = false;
    for (size_t k = 0; k < infos.size(); ++k) {
        const EffectParamInfo &info = infos[k];
        const double moved =
            std::clamp(effect_param(layer.spec, k) + setting_offsets[k], info.min, info.max);
        const bool ring = route_values != nullptr && !setting_routes[k].empty();
        if ((k + 1) % static_cast<size_t>(columns) != 0) {
            ImGui::SameLine();
        }
        const std::string label = std::string(info.label) + "##" + info.id;
        const std::string unit = info.unit[0] != 0 ? std::string(" ") + info.unit : std::string();
        set_knob_menu([&] {
            modulation_menu(scene, static_cast<int>(index), ModTarget::setting, info.id,
                            effect_param(layer.spec, k), info.min, info.max);
        });
        if (info.integer) {
            int v = static_cast<int>(std::lround(effect_param(layer.spec, k)));
            // A named value shows its name (as a format with no conversions, '%' escaped).
            std::string format = "%.0f" + unit;
            if (const std::string option = effect_option_name(info, v); !option.empty()) {
                format.clear();
                for (const char ch : option) {
                    format += ch == '%' ? std::string("%%") : std::string(1, ch);
                }
            }
            if (knob_int(label.c_str(), &v, static_cast<int>(info.min), static_cast<int>(info.max),
                         format.c_str(), static_cast<int>(std::lround(effect_param(defaults, k))),
                         ring ? &moved : nullptr)) {
                set_effect_param(layer.spec, k, v);
                params_changed = true;
            }
        } else {
            double v = effect_param(layer.spec, k);
            if (knob(label.c_str(), &v, info.min, info.max, ("%.3g" + unit).c_str(),
                     effect_param(defaults, k), ring ? &moved : nullptr)) {
                set_effect_param(layer.spec, k, v);
                params_changed = true;
            }
        }
        if (!setting_routes[k].empty()) {
            ImGui::SetItemTooltip("%s, modulated by:%s", info.description,
                                  setting_routes[k].c_str());
        } else if (info.description[0] != '\0') {
            ImGui::SetItemTooltip("%s. Drag (Shift: fine); double-click to type; right-click "
                                  "for reset and modulation.",
                                  info.description);
        }
    }
    if (params_changed) {
        layer.effect = make_effect(layer.spec);
        scene_edited_ = true;
    }
    end_device();
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("psyight layer")) {
            move_from = *static_cast<const int *>(payload->Data);
            move_to = static_cast<int>(index);
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
}

std::string PreviewView::source_label(ModSource source) const {
    const int macro = static_cast<int>(source) - static_cast<int>(ModSource::macro_1);
    return macro >= 0 && macro < kMacroCount ? macro_name(project_.project(), macro)
                                             : mod_source_name(source);
}

void PreviewView::draw_macros() {
    const ImGuiStyle &style = ImGui::GetStyle();
    constexpr int kColumns = 4;
    const float width = 2.0f * device_padding() + kColumns * knob_width() +
                        static_cast<float>(kColumns - 1) * style.ItemSpacing.x;
    const float height =
        device_title_height() + 2.0f * knob_height() + style.ItemSpacing.y + device_padding();
    begin_device("##macros", ImVec2(width, height));
    push_device_title();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Macros");
    const float rename_width = ImGui::CalcTextSize(kIconRename).x + 2.0f * style.FramePadding.x;
    ImGui::SameLine(ImGui::GetWindowWidth() - device_padding() - rename_width);
    if (device_icon_button(kIconRename, "rename", "Name the macros.")) {
        ImGui::OpenPopup("##macro names");
    }
    pop_device_title();
    ImGui::SetCursorPosY(device_title_height());
    draw_macro_knobs(kColumns);
    if (ImGui::BeginPopup("##macro names")) {
        for (int i = 0; i < kMacroCount; ++i) {
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(10.0f * ImGui::GetFontSize());
            const std::string hint = "Macro " + std::to_string(i + 1);
            if (ImGui::InputTextWithHint("##name", hint.c_str(),
                                         &project_.edit().macro_names[static_cast<size_t>(i)])) {
                project_.changed();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    end_device();
}

void PreviewView::draw_macro_knobs(int columns) {
    for (int i = 0; i < kMacroCount; ++i) {
        if (i % columns != 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(i);
        double value = project_.project().macro_values[static_cast<size_t>(i)];
        const std::string target = "macro:" + std::to_string(i);
        set_knob_menu([&] { midi_menu(target); });
        if (knob(macro_name(project_.project(), i).c_str(), &value, 0.0, 1.0, "%.2f", 0.0)) {
            project_.edit().macro_values[static_cast<size_t>(i)] = value;
            project_.changed();
        }
        if (midi_map_.learning() == target) {
            ImGui::SetItemTooltip("Learning: move a control on the controller.");
        } else if (const MidiBinding *bound = midi_map_.binding_of(target); bound != nullptr) {
            ImGui::SetItemTooltip("%s: %s. A modulation source.",
                                  macro_name(project_.project(), i).c_str(),
                                  describe(*bound).c_str());
        } else {
            ImGui::SetItemTooltip("%s: a modulation source. Right-click to learn a MIDI knob "
                                  "for it.",
                                  macro_name(project_.project(), i).c_str());
        }
        ImGui::PopID();
    }
}

void PreviewView::draw_perform(bool *open) {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowFocus();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("Perform", nullptr, flags);
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        *open = false;
    }
    const std::shared_ptr<const ShowFrame> frame = show_.latest();
    const ImGuiStyle &style = ImGui::GetStyle();
    const float em = ImGui::GetFontSize();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side = std::min(30.0f * em, 0.42f * avail.x);

    // The stage: the beat, section and tempo, then the rig, large.
    ImGui::BeginChild("##stage", ImVec2(avail.x - side - style.ItemSpacing.x, 0.0f));
    if (frame != nullptr) {
        ImGui::PushFont(theme_font(ThemeFont::timecode));
        ImGui::TextUnformatted(frame->section.empty() ? "-" : frame->section.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        // Four beat dots, the current one lit.
        const float r = 0.45f * em;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        for (int b = 0; b < 4; ++b) {
            const ImVec2 c(at.x + r + b * 3.0f * r, at.y + 0.75f * em);
            draw->AddCircleFilled(
                c, r, to_u32(b == frame->beat_in_bar ? theme().accent : theme().frame_hover));
        }
        ImGui::Dummy(ImVec2(12.0f * r, 1.5f * em));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (const std::shared_ptr<const LiveInput> live = session_.live_input(); live) {
            const LiveState s = live->state();
            if (s.tempo_known) {
                mono_text("%.1f BPM  %s", 60.0 / s.period_s, s.locked ? "locked" : "acquiring");
            } else {
                mono_text("listening");
            }
        } else {
            mono_text("%.1f BPM", config_.grid.bpm);
        }
        if (config_.blackout || config_.hold) {
            ImGui::SameLine();
            ImGui::TextColored(config_.blackout ? theme().danger : theme().accent, "%s",
                               config_.blackout ? "BLACKOUT" : "HOLD");
        }
        draw_rig(*frame, false); // Perform: viewing only.
    } else {
        ImGui::TextDisabled("No show yet: load a track or start live input.");
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // The controls.
    ImGui::BeginChild("##controls", ImVec2(0.0f, 0.0f));
    const float width = ImGui::GetContentRegionAvail().x;
    constexpr int kColumns = 3;
    const float button_w = (width - (kColumns - 1) * style.ItemSpacing.x) / kColumns;
    const ImVec2 big(button_w, 3.0f * em);
    ImGui::SeparatorText("Scenes");
    ImGui::BeginDisabled(ladder_ == Ladder::ambient);
    for (int i = 0; i < static_cast<int>(config_.scenes.size()); ++i) {
        if (i % kColumns != 0) {
            ImGui::SameLine();
        }
        const bool playing = frame != nullptr && frame->mix.to == i;
        if (playing) {
            push_filled_button(theme().accent);
        }
        ImGui::PushID(i);
        char label[96];
        const char *name = config_.scenes[static_cast<size_t>(i)].name.c_str();
        if (i < 9) { // Keys 1 to 9.
            std::snprintf(label, sizeof(label), "%d  %s", i + 1, name);
        } else {
            std::snprintf(label, sizeof(label), "%s", name);
        }
        if (ImGui::Button(label, big)) {
            pick_scene(i);
        }
        ImGui::PopID();
        if (playing) {
            pop_filled_button();
        }
        if (frame != nullptr && frame->queued_scene == i) {
            const float pulse =
                0.5f + 0.5f * static_cast<float>(std::sin(ImGui::GetTime() * 2.0 * kPi * 2.0));
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                to_u32(with_alpha(theme().accent, 0.4f + 0.6f * pulse)), style.FrameRounding, 0,
                3.0f);
        }
    }
    const bool automatic = config_.manual_scene < 0;
    if (toggle_button("Auto: scenes follow the sections (0)###auto", automatic)) {
        pick_scene(automatic ? std::max(0, frame != nullptr ? frame->mix.to : 0) : -1);
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Macros");
    // On a device-coloured panel, where the knob tracks are tuned to show.
    push_knob_scale(1.5f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme().device_bg);
    ImGui::BeginChild(
        "##perform macros",
        ImVec2(width, 2.0f * knob_height() + style.ItemSpacing.y + 2.0f * style.WindowPadding.y),
        ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    draw_macro_knobs(4);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    pop_knob_scale();

    ImGui::SeparatorText("Safety");
    const ImVec2 half((width - style.ItemSpacing.x) / 2.0f, 3.0f * em);
    const auto safety = [&](const char *label, bool &on, const ImVec4 &colour) {
        if (on) {
            push_filled_button(colour);
        }
        if (ImGui::Button(label, half)) {
            on = !on;
            dirty_ = true;
        }
        if (on) {
            pop_filled_button();
        }
    };
    safety("Blackout (B)", config_.blackout, theme().danger);
    ImGui::SameLine();
    safety("Hold (H)", config_.hold, theme().accent);
    ImGui::Spacing();
    if (ImGui::Button("Exit the Perform view (Esc)", ImVec2(width, 0.0f))) {
        *open = false;
    }
    ImGui::EndChild();
    ImGui::End();
}

void PreviewView::draw_scene_cards(const ShowFrame &frame) {
    const ImGuiStyle &style = ImGui::GetStyle();
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const bool manual = config_.manual_scene >= 0;
    ImGui::BeginDisabled(ladder_ == Ladder::ambient); // Ambient plays the ambient scene only.
    for (int i = 0; i < static_cast<int>(config_.scenes.size()); ++i) {
        const std::string &name = config_.scenes[static_cast<size_t>(i)].name;
        const float width = ImGui::CalcTextSize(name.c_str()).x + 2.0f * style.FramePadding.x;
        if (i > 0 && ImGui::GetItemRectMax().x + style.ItemSpacing.x + width < right) {
            ImGui::SameLine();
        }
        const bool playing = frame.mix.to == i;
        const bool fading_out = frame.mix.from == i;
        if (playing || fading_out) {
            // The playing scene is an active toggle: the accent; half the accent while it fades
            // out, with the normal text (dark text would be lost on it).
            if (playing) {
                push_filled_button(theme().accent);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, with_alpha(theme().accent, 0.5f));
            }
        }
        ImGui::PushID(i);
        if (ImGui::Button(name.c_str())) {
            pick_scene(i);
        }
        ImGui::PopID();
        if (frame.queued_scene == i) { // Waiting for its launch: a pulsing outline.
            const float pulse =
                0.5f + 0.5f * static_cast<float>(std::sin(ImGui::GetTime() * 2.0 * kPi * 2.0));
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                to_u32(with_alpha(theme().accent, 0.4f + 0.6f * pulse)),
                ImGui::GetStyle().FrameRounding, 0, 2.0f);
        }
        if (playing) {
            pop_filled_button();
        } else if (fading_out) {
            ImGui::PopStyleColor();
        }
        std::string tags;
        for (const std::string &s : config_.scenes[static_cast<size_t>(i)].sections) {
            tags += (tags.empty() ? "" : ", ") + s;
        }
        ImGui::SetItemTooltip("For: %s. Click (or press %d) to hold this scene.",
                              tags.empty() ? "manual use only" : tags.c_str(), i + 1);
    }
    bool automatic = !manual;
    if (ImGui::Checkbox("Auto (by section)", &automatic)) {
        pick_scene(automatic ? -1 : std::max(0, frame.mix.to));
    }
    ImGui::SetItemTooltip("Scenes follow the sections; clicking a scene holds it until this is "
                          "ticked again (or 0 is pressed).");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(7.0f * ImGui::GetFontSize());
    const SceneLaunch launch = project_.project().automation.launch;
    if (ImGui::BeginCombo("##launch", scene_launch_name(launch))) {
        for (const SceneLaunch l : kSceneLaunches) {
            if (ImGui::Selectable(scene_launch_name(l), l == launch)) {
                project_.edit().automation.launch = l;
                project_.changed();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("When a scene you pick (or the return to Auto) takes over. Keys 1 to 9 "
                          "pick scenes, 0 returns to the sections.");
    ImGui::EndDisabled();
    if (frame.queued_scene != SceneLauncher::kNoneQueued) {
        ImGui::SameLine();
        ImGui::TextColored(
            theme().accent, "%s at the %s",
            frame.queued_scene < 0 ? "Auto"
            : frame.queued_scene < static_cast<int>(config_.scenes.size())
                ? config_.scenes[static_cast<size_t>(frame.queued_scene)].name.c_str()
                : "?",
            launch == SceneLaunch::phrase ? "next phrase" : "next bar");
    }
    if (frame.mix.from >= 0) {
        ImGui::SameLine();
        mono_text("crossfade %.0f%%", 100.0 * frame.mix.to_weight);
    }
    if (frame.mix.palette_shift != 0) {
        ImGui::SameLine();
        mono_text("palette +%d", frame.mix.palette_shift);
    }
}

void PreviewView::draw_output(const ShowFrame &frame) {
    const float em = ImGui::GetFontSize();
    OutputSettings o = project_.project().output;
    int first_universe = project_.project().first_universe;
    bool changed = false;
    if (config_.send_to_network) {
        ImGui::TextColored(theme().danger,
                           "Sending to the network (top bar: Ambient, React or Show).");
    } else {
        ImGui::TextUnformatted("Not sending: the virtual node (this preview) only. The top bar's "
                               "Ambient, React and Show send.");
    }
    ImGui::BeginDisabled(!tier_at_least(Tier::tune));
    int protocol = o.protocol == OutputProtocol::sacn ? 0 : 1;
    ImGui::SetNextItemWidth(8.5f * em);
    if (ImGui::Combo("Protocol", &protocol, "sACN (E1.31)\0Art-Net 4\0")) {
        o.protocol = protocol == 0 ? OutputProtocol::sacn : OutputProtocol::art_net;
        changed = true;
    }
    if (o.protocol == OutputProtocol::sacn) {
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Multicast", &o.multicast);
        ImGui::SetItemTooltip("239.255.hi.lo per universe. Switches need IGMP snooping, or "
                              "multicast floods the network (SPEC 8).");
    }
    if (o.protocol == OutputProtocol::art_net || !o.multicast) {
        ImGui::SetNextItemWidth(8.0f * em);
        if (!address_active_ && parse_ipv4(address_text_) != o.address) {
            address_text_ = ipv4_text(o.address); // Loaded or reset elsewhere.
        }
        if (ImGui::InputText("Node address", &address_text_)) {
            if (const auto address = parse_ipv4(address_text_)) {
                o.address = *address;
                changed = true;
            }
        }
        address_active_ = ImGui::IsItemActive();
        if (!parse_ipv4(address_text_)) {
            ImGui::SameLine();
            ImGui::TextColored(theme().danger, "not an IPv4 address");
        }
    }
    ImGui::SetNextItemWidth(6.0f * em);
    if (mono_field("First universe",
                   [&](const char *id) { return ImGui::InputInt(id, &first_universe); })) {
        first_universe = std::clamp(first_universe, 1, 63000);
        changed = true;
    }
    changed |= ImGui::Checkbox("Sync", &o.sync);
    if (o.sync && o.protocol == OutputProtocol::sacn) {
        ImGui::SameLine();
        int sync_universe = o.sync_universe;
        ImGui::SetNextItemWidth(6.0f * em);
        if (mono_field("Sync universe",
                       [&](const char *id) { return ImGui::InputInt(id, &sync_universe); })) {
            o.sync_universe = static_cast<std::uint16_t>(std::clamp(sync_universe, 1, 63999));
            changed = true;
        }
    }
    ImGui::EndDisabled();
    if (changed) {
        Project &p = project_.edit();
        p.output = o;
        p.first_universe = first_universe;
        project_.changed();
    }
    const std::vector<int> &universes = project_.patch().universes;
    if (o.protocol == OutputProtocol::sacn && o.sync &&
        std::find(universes.begin(), universes.end(), o.sync_universe) != universes.end()) {
        ImGui::TextColored(theme().danger, "The sync universe carries pixel data.");
    }
    mono_text("Universes %d to %d, %d packets a frame, %.1f fps",
              universes.empty() ? 0 : universes.front(), universes.empty() ? 0 : universes.back(),
              frame.packets_sent, measured_fps_);
    if (!tier_at_least(Tier::engineer)) {
        return;
    }
    mono_text("Frame cost %.2f ms, %lld late frames", frame.render_ms,
              static_cast<long long>(frame.late_frames));
    ImGui::SetItemTooltip("Render, limiters, patch and send, measured on the show thread.");
    mono_text("Node: %lld packets, %lld latched, %lld gaps, %lld bad",
              static_cast<long long>(frame.node.packets),
              static_cast<long long>(frame.node.latches),
              static_cast<long long>(frame.node.sequence_gaps),
              static_cast<long long>(frame.node.bad_packets));
    if (frame.send_errors > 0 || !frame.network_error.empty()) {
        ImGui::TextColored(theme().danger, "Network: %lld send errors. %s",
                           static_cast<long long>(frame.send_errors), frame.network_error.c_str());
    }
}

void PreviewView::draw_rig(const ShowFrame &frame, bool tools) {
    // The pixels, zoom and pan, hover arrows and the strip editor (gui/stage_canvas.h).
    canvas_.draw(config_.rig, frame.node_rgb, config_.limiter.gamma, highlighted_strip_, tools);
    hovered_strip_ = canvas_.hovered_strip();
}

void PreviewView::draw_library() {
    if (ImGui::Button("Show packs")) {
        user_presets_ = load_user_presets(presets_dir_);
        ImGui::OpenPopup("##show packs");
    }
    ImGui::SetItemTooltip("Whole show styles: scenes, palettes, automation and macro names.");
    if (!preset_status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", preset_status_.c_str());
    }
    bool confirm = false;
    bool save = false;
    if (ImGui::BeginPopup("##show packs")) {
        const auto item = [&](const ShowPack &pack) {
            if (ImGui::Selectable(pack.name.c_str())) {
                pending_pack_ = pack;
                confirm = true;
            }
            if (!pack.description.empty()) {
                ImGui::SetItemTooltip("%s", pack.description.c_str());
            }
        };
        ImGui::SeparatorText("Built-in");
        for (const ShowPack &pack : builtin_show_packs()) {
            item(pack);
        }
        if (!user_presets_.shows.empty()) {
            ImGui::SeparatorText("Yours");
            for (const ShowPack &pack : user_presets_.shows) {
                item(pack);
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Save this show as a pack...")) {
            preset_name_ = "My show";
            preset_description_.clear();
            save = true;
        }
        ImGui::EndPopup();
    }
    if (confirm) {
        ImGui::OpenPopup("Load show pack?");
    }
    if (save) {
        ImGui::OpenPopup("Save show pack");
    }
    if (ImGui::BeginPopupModal("Load show pack?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (pending_pack_) {
            ImGui::Text("Replace the scenes, palettes and automation with %s?",
                        pending_pack_->name.c_str());
            ImGui::TextDisabled("Macro values, the rig, limiters and output stay.");
            if (primary_button("Load")) {
                apply_show_pack(project_.edit(), *pending_pack_);
                project_.changed();
                edit_scene_ = 0;
                config_.manual_scene = -1;
                dirty_ = true;
                preset_status_ = "Loaded " + pending_pack_->name + ".";
                pending_pack_.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel")) {
            pending_pack_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Save show pack", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(16.0f * ImGui::GetFontSize());
        ImGui::InputText("Name", &preset_name_);
        ImGui::SetNextItemWidth(16.0f * ImGui::GetFontSize());
        ImGui::InputText("Description", &preset_description_);
        const std::filesystem::path path =
            preset_path(presets_dir_, PresetKind::show, preset_name_);
        if (std::filesystem::exists(path)) {
            ImGui::TextColored(theme().warning, "Replaces %s.",
                               path_to_utf8(path.filename()).c_str());
        }
        if (primary_button("Save")) {
            ShowPack pack = show_pack_of(project_.project(), preset_name_);
            pack.description = preset_description_;
            const Result<void> saved = save_show_pack(presets_dir_, pack);
            preset_status_ = saved ? "Saved " + path_to_utf8(path) + "." : saved.error().message;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void PreviewView::draw_scene_presets() {
    if (ImGui::Button("Presets")) {
        user_presets_ = load_user_presets(presets_dir_);
        ImGui::OpenPopup("##scene presets");
    }
    ImGui::SetItemTooltip("Load a look into this scene, or add it as a new one; save this one.");
    bool save = false;
    if (ImGui::BeginPopup("##scene presets")) {
        const auto item = [&](const ScenePreset &preset) {
            if (ImGui::BeginMenu(preset.name.c_str())) {
                if (ImGui::MenuItem("Replace this scene")) {
                    pending_scene_ = preset;
                    pending_scene_replace_ = true;
                }
                if (ImGui::MenuItem("Add as a new scene")) {
                    pending_scene_ = preset;
                    pending_scene_replace_ = false;
                }
                ImGui::EndMenu();
            }
            if (!preset.description.empty()) {
                ImGui::SetItemTooltip("%s", preset.description.c_str());
            }
        };
        ImGui::SeparatorText("Built-in");
        for (const ScenePreset &preset : builtin_scene_presets()) {
            item(preset);
        }
        if (!user_presets_.scenes.empty()) {
            ImGui::SeparatorText("Yours");
            for (const ScenePreset &preset : user_presets_.scenes) {
                item(preset);
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Save this scene as a preset...")) {
            preset_name_ = project_.project().scenes[static_cast<size_t>(edit_scene_)].name;
            preset_description_.clear();
            save = true;
        }
        ImGui::EndPopup();
    }
    if (save) {
        ImGui::OpenPopup("Save scene preset");
    }
    if (ImGui::BeginPopupModal("Save scene preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(16.0f * ImGui::GetFontSize());
        ImGui::InputText("Name", &preset_name_);
        ImGui::SetNextItemWidth(16.0f * ImGui::GetFontSize());
        ImGui::InputText("Description", &preset_description_);
        const std::filesystem::path path =
            preset_path(presets_dir_, PresetKind::scene, preset_name_);
        if (std::filesystem::exists(path)) {
            ImGui::TextColored(theme().warning, "Replaces %s.",
                               path_to_utf8(path.filename()).c_str());
        }
        if (primary_button("Save")) {
            ScenePreset preset = scene_preset_of(project_.project(), edit_scene_);
            preset.name = preset_name_;
            preset.scene.name = preset_name_;
            preset.description = preset_description_;
            const Result<void> saved = save_scene_preset(presets_dir_, preset);
            preset_status_ = saved ? "Saved " + path_to_utf8(path) + "." : saved.error().message;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void PreviewView::apply_pending_scene_preset() {
    if (!pending_scene_) {
        return;
    }
    edit_scene_ = apply_scene_preset(project_.edit(), *pending_scene_,
                                     pending_scene_replace_ ? edit_scene_ : -1);
    preset_status_ = (pending_scene_replace_ ? "Loaded " : "Added ") + pending_scene_->name + ".";
    pending_scene_.reset();
    scene_edited_ = true;
}

void PreviewView::draw_palette_library() {
    if (ImGui::Button("Add from the library")) {
        user_presets_ = load_user_presets(presets_dir_);
        ImGui::OpenPopup("##palette library");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save to the library")) {
        ImGui::OpenPopup("##save palette");
    }
    ImGui::SetItemTooltip("Save one of this project's palettes as a preset.");
    const auto swatches = [](const Palette &palette) {
        for (size_t c = 0; c < palette.colours.size(); ++c) {
            const Rgb &rgb = palette.colours[c];
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(c));
            ImGui::ColorButton("##swatch", ImVec4(rgb.r, rgb.g, rgb.b, 1.0f),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                               ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize()));
            ImGui::PopID();
        }
    };
    if (ImGui::BeginPopup("##palette library")) {
        const auto item = [&](const Palette &palette) {
            ImGui::PushID(palette.name.c_str());
            if (ImGui::Selectable(palette.name.c_str(), false, 0,
                                  ImVec2(8.0f * ImGui::GetFontSize(), 0.0f))) {
                add_palette(project_.edit(), palette);
                project_.changed();
                preset_status_ = "Added the palette " + palette.name + ".";
            }
            swatches(palette);
            ImGui::PopID();
        };
        ImGui::SeparatorText("Built-in");
        for (const Palette &palette : builtin_palettes()) {
            item(palette);
        }
        if (!user_presets_.palettes.empty()) {
            ImGui::SeparatorText("Yours");
            for (const Palette &palette : user_presets_.palettes) {
                item(palette);
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##save palette")) {
        for (const Palette &palette : project_.project().palettes) {
            ImGui::PushID(palette.name.c_str());
            if (ImGui::Selectable(palette.name.c_str(), false, 0,
                                  ImVec2(8.0f * ImGui::GetFontSize(), 0.0f))) {
                const Result<void> saved = save_palette_preset(presets_dir_, palette);
                preset_status_ =
                    saved ? "Saved the palette " + palette.name + "." : saved.error().message;
            }
            swatches(palette);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
}

std::vector<std::uint8_t> PreviewView::audition_bytes(const ShowFrame &frame) const {
    if (!audition_ || (config_.analysis == nullptr && config_.live == nullptr)) {
        return {};
    }
    // The engine inputs at the show's render time, as the show thread had them.
    EngineInputs inputs;
    std::optional<LiveState> live;
    if (config_.live != nullptr) {
        live = config_.live->state();
        inputs = inputs_at(*live, frame.t_s);
    } else {
        inputs = inputs_at(*config_.analysis, config_.grid, frame.t_s, config_.sections.get());
    }
    inputs.macros = config_.macros;
    Frame out;
    const Audition &a = *audition_;
    switch (a.kind) {
    case BrowserTab::scenes:
        out = render_scene(a.scene.scene, config_.rig, inputs, a.palette, config_.seed);
        break;
    case BrowserTab::palettes: {
        if (config_.scenes.empty()) {
            return {};
        }
        const int playing =
            std::clamp(frame.mix.to, 0, static_cast<int>(config_.scenes.size()) - 1);
        out = render_scene(config_.scenes[static_cast<size_t>(playing)], config_.rig, inputs,
                           a.palette, config_.seed);
        break;
    }
    case BrowserTab::shows: {
        const SceneMix mix = live ? automate_live(a.show.scenes, *live, inputs, a.show.automation)
                                  : automate(a.show.scenes, *config_.analysis, config_.grid, inputs,
                                             a.show.automation, -1, config_.sections.get());
        out = render_mix(a.show.scenes, mix, config_.rig, inputs, a.show.palettes, config_.seed);
        break;
    }
    }
    const double cap = std::clamp(config_.limiter.brightness_cap, 0.0, 1.0);
    for (Rgb &c : out) {
        c = scaled(c, cap);
    }
    return to_bytes(out, config_.limiter.gamma);
}

void PreviewView::draw_browser(bool *open) {
    if (!*open) {
        audition_.reset();
        browser_scanned_ = false;
        return;
    }
    if (!browser_scanned_) {
        user_presets_ = load_user_presets(presets_dir_);
        browser_scanned_ = true;
    }
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(28.0f * em, 30.0f * em), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Browser", open)) {
        audition_.reset();
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Rescan").x -
                            3.0f * ImGui::GetStyle().FramePadding.x);
    ImGui::InputTextWithHint("##search", "Search presets", &browser_search_);
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) {
        user_presets_ = load_user_presets(presets_dir_);
    }
    ImGui::SetItemTooltip("Read the presets folder again (%s).",
                          path_to_utf8(presets_dir_).c_str());
    const auto lower = [](std::string text) {
        for (char &c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    };
    const std::string needle = lower(browser_search_);
    const auto matches = [&](const std::string &name, const std::string &description) {
        return needle.empty() || lower(name).find(needle) != std::string::npos ||
               lower(description).find(needle) != std::string::npos;
    };
    std::optional<Audition> hovered;
    std::optional<Audition> selected;
    const auto row = [&](const std::string &key, const std::string &name,
                         const std::string &description, bool yours, const Audition &audition) {
        ImGui::PushID(key.c_str());
        const bool is_selected = browser_selected_ == key;
        if (ImGui::Selectable("##row", is_selected, 0,
                              ImVec2(0.0f, 2.0f * ImGui::GetTextLineHeight()))) {
            browser_selected_ = is_selected ? std::string() : key;
        }
        if (ImGui::IsItemHovered()) {
            hovered = audition;
        }
        if (browser_selected_ == key) {
            selected = audition;
        }
        ImGui::SameLine(ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginGroup();
        ImGui::TextUnformatted(name.c_str());
        if (yours) {
            ImGui::SameLine();
            ImGui::TextColored(theme().info, "yours");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, theme().text_dim);
        ImGui::TextUnformatted(description.empty() ? " " : description.c_str());
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::PopID();
    };
    if (ImGui::BeginTabBar("##browser tabs")) {
        const auto tab = [&](const char *label, BrowserTab which) {
            if (ImGui::BeginTabItem(label)) {
                if (browser_tab_ != which) {
                    browser_tab_ = which;
                    browser_selected_.clear();
                }
                ImGui::EndTabItem();
            }
        };
        tab("Scenes", BrowserTab::scenes);
        tab("Palettes", BrowserTab::palettes);
        tab("Show packs", BrowserTab::shows);
        ImGui::EndTabBar();
    }
    const float footer = 2.0f * ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##list", ImVec2(0.0f, -footer));
    switch (browser_tab_) {
    case BrowserTab::scenes: {
        const auto list = [&](const std::vector<ScenePreset> &presets, bool yours) {
            for (const ScenePreset &p : presets) {
                if (matches(p.name, p.description)) {
                    Audition a;
                    a.kind = BrowserTab::scenes;
                    a.scene = p;
                    a.palette = p.palette;
                    a.name = p.name;
                    row(std::string(yours ? "u" : "b") + "s:" + p.name, p.name, p.description,
                        yours, a);
                }
            }
        };
        list(builtin_scene_presets(), false);
        list(user_presets_.scenes, true);
        break;
    }
    case BrowserTab::palettes: {
        const auto list = [&](const std::vector<Palette> &palettes, bool yours) {
            for (const Palette &p : palettes) {
                if (matches(p.name, "")) {
                    Audition a;
                    a.kind = BrowserTab::palettes;
                    a.palette = p;
                    a.name = p.name;
                    row(std::string(yours ? "u" : "b") + "p:" + p.name, p.name,
                        "On the playing scene.", yours, a);
                }
            }
        };
        list(builtin_palettes(), false);
        list(user_presets_.palettes, true);
        break;
    }
    case BrowserTab::shows: {
        const auto list = [&](const std::vector<ShowPack> &packs, bool yours) {
            for (const ShowPack &p : packs) {
                if (matches(p.name, p.description)) {
                    Audition a;
                    a.kind = BrowserTab::shows;
                    a.show = p;
                    a.name = p.name;
                    row(std::string(yours ? "u" : "b") + "w:" + p.name, p.name, p.description,
                        yours, a);
                }
            }
        };
        list(builtin_show_packs(), false);
        list(user_presets_.shows, true);
        break;
    }
    }
    ImGui::EndChild();

    // The selected preset: its actions.
    ImGui::Separator();
    if (!selected) {
        ImGui::TextDisabled("Hover a preset to see it on the preview; click to select it.");
    } else if (selected->kind == BrowserTab::scenes) {
        const std::string scene =
            edit_scene_ < static_cast<int>(project_.project().scenes.size())
                ? project_.project().scenes[static_cast<size_t>(edit_scene_)].name
                : std::string("the scene");
        if (primary_button(("Replace " + scene).c_str())) {
            pending_scene_ = selected->scene;
            pending_scene_replace_ = true;
            apply_pending_scene_preset();
            project_.changed();
            scene_edited_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add as a new scene")) {
            pending_scene_ = selected->scene;
            pending_scene_replace_ = false;
            apply_pending_scene_preset();
            project_.changed();
            scene_edited_ = false;
        }
    } else if (selected->kind == BrowserTab::palettes) {
        if (primary_button("Add to the project")) {
            add_palette(project_.edit(), selected->palette);
            project_.changed();
            preset_status_ = "Added the palette " + selected->palette.name + ".";
        }
    } else {
        if (primary_button("Load this show pack...")) {
            pending_pack_ = selected->show;
            ImGui::OpenPopup("Load show pack?##browser");
        }
    }
    if (ImGui::BeginPopupModal("Load show pack?##browser", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (pending_pack_) {
            ImGui::Text("Replace the scenes, palettes and automation with %s?",
                        pending_pack_->name.c_str());
            ImGui::TextDisabled("Macro values, the rig, limiters and output stay.");
            if (primary_button("Load")) {
                apply_show_pack(project_.edit(), *pending_pack_);
                project_.changed();
                edit_scene_ = 0;
                config_.manual_scene = -1;
                dirty_ = true;
                preset_status_ = "Loaded " + pending_pack_->name + ".";
                pending_pack_.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel")) {
            pending_pack_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!preset_status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", preset_status_.c_str());
    }
    audition_ = hovered ? hovered : selected;
    ImGui::End();
}

} // namespace psyight
