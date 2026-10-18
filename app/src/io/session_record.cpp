#include "io/session_record.h"

#include "core/paths.h"
#include "io/text_file.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace psyight {

namespace {

using nlohmann::json;

constexpr std::array<char, 8> kFramesMagic = {'P', 'S', 'Y', 'F', 'R', 'M', '0', '1'};
constexpr size_t kFramesHeaderBytes = 12;
constexpr size_t kRecordFixedBytes = 8 + 8 + 8 + 1; // t_s, index, config version, flags.

json grid_json(const Grid &g) {
    return {
        {"bpm", g.bpm}, {"first_downbeat", g.first_downbeat}, {"sample_rate_hz", g.sample_rate_hz}};
}

Grid parse_grid(const json &j) {
    return {j.at("bpm").get<double>(), j.at("first_downbeat").get<SampleIndex>(),
            j.at("sample_rate_hz").get<int>()};
}

// Little-endian, as on every target this runs on; copied byte by byte to stay portable.
template <typename T> void put(std::vector<std::uint8_t> &out, T value) {
    std::array<std::uint8_t, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    out.insert(out.end(), bytes.begin(), bytes.end());
}

template <typename T> T get(std::span<const std::uint8_t> in, size_t at) {
    T value{};
    std::memcpy(&value, in.data() + at, sizeof(T));
    return value;
}

} // namespace

Result<std::string> meta_json(const SessionMeta &m) {
    try {
        const json root = {
            {"schema", kSessionSchemaVersion},
            {"app_version", m.app_version},
            {"track", path_to_utf8(m.track)},
            {"source", m.source == AnalysisSource::stems ? "stems" : "mix"},
            {"analysis_grid", grid_json(m.analysis_grid)},
            {"pixels", m.pixels},
            {"frame_rate_hz", m.frame_rate_hz},
        };
        return root.dump(2) + "\n";
    } catch (const json::exception &e) {
        return Error{std::string("cannot write session meta: ") + e.what()};
    }
}

Result<SessionMeta> parse_meta_json(std::string_view text) {
    try {
        const json root = json::parse(text);
        if (root.at("schema").get<int>() != kSessionSchemaVersion) {
            return Error{"unsupported session schema"};
        }
        SessionMeta m;
        m.app_version = root.at("app_version").get<std::string>();
        const std::string track = root.at("track").get<std::string>();
        m.track = std::filesystem::path(std::u8string(track.begin(), track.end()));
        m.source = root.at("source").get<std::string>() == "stems" ? AnalysisSource::stems
                                                                   : AnalysisSource::mix;
        m.analysis_grid = parse_grid(root.at("analysis_grid"));
        m.pixels = root.at("pixels").get<int>();
        m.frame_rate_hz = root.at("frame_rate_hz").get<double>();
        return m;
    } catch (const json::exception &e) {
        return Error{std::string("invalid session meta: ") + e.what()};
    }
}

static json sections_json(const std::vector<DetectedSection> &sections) {
    json out = json::array();
    for (const DetectedSection &s : sections) {
        out.push_back({{"start_s", s.start_s}, {"end_s", s.end_s}, {"label", s.label}});
    }
    return out;
}

Result<std::string> params_line(std::uint64_t version, const Project &project,
                                const RuntimeState &runtime) {
    const Result<std::string> project_text = to_project_json(project);
    if (!project_text) {
        return project_text.error();
    }
    try {
        const json line = {
            {"version", version},
            {"project", json::parse(project_text.value())},
            {"runtime",
             {{"manual_scene", runtime.manual_scene},
              {"blackout", runtime.blackout},
              {"hold", runtime.hold},
              {"grid", grid_json(runtime.grid)},
              {"sections", sections_json(runtime.sections)}}},
        };
        return line.dump() + "\n";
    } catch (const json::exception &e) {
        return Error{std::string("cannot write session params: ") + e.what()};
    }
}

Result<std::vector<ParamsEntry>> parse_params_jsonl(std::string_view text) {
    std::vector<ParamsEntry> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (line.find_first_not_of(" \r\t") == std::string_view::npos) {
            continue;
        }
        try {
            const json j = json::parse(line);
            ParamsEntry e;
            e.version = j.at("version").get<std::uint64_t>();
            Result<Project> project = parse_project_json(j.at("project").dump());
            if (!project) {
                return project.error();
            }
            e.project = std::move(project.value());
            const json &r = j.at("runtime");
            e.runtime.manual_scene = r.at("manual_scene").get<int>();
            e.runtime.blackout = r.at("blackout").get<bool>();
            e.runtime.hold = r.at("hold").get<bool>();
            e.runtime.grid = parse_grid(r.at("grid"));
            if (const auto sections = r.find("sections"); sections != r.end()) {
                for (const json &s : sections->get_ref<const json::array_t &>()) {
                    DetectedSection d;
                    d.start_s = s.at("start_s").get<double>();
                    d.end_s = s.at("end_s").get<double>();
                    d.label = s.at("label").get<std::string>();
                    d.confidence = 1.0;
                    e.runtime.sections.push_back(std::move(d));
                }
            }
            out.push_back(std::move(e));
        } catch (const json::exception &e) {
            return Error{std::string("invalid session params: ") + e.what()};
        }
    }
    return out;
}

std::string events_jsonl(const TrackAnalysis &a) {
    std::string out;
    const auto line = [&out](const json &j) { out += j.dump() + "\n"; };
    for (const DetectedSection &s : a.sections) {
        line({{"type", "section"},
              {"start_s", s.start_s},
              {"end_s", s.end_s},
              {"label", s.label},
              {"confidence", s.confidence}});
    }
    for (const double t : a.bass.kick_times_s) {
        line({{"type", "kick"}, {"t_s", t}});
    }
    for (const BassNote &n : a.bass.notes) {
        line({{"type", "note"},
              {"onset_s", n.onset_s},
              {"offset_s", n.offset_s},
              {"midi", n.pitch_midi},
              {"cents", n.cents},
              {"gate_ratio", n.gate_ratio},
              {"velocity", n.velocity},
              {"confidence", n.confidence}});
    }
    for (const HatOnset &h : a.residual.hats) {
        line({{"type", "hat"}, {"t_s", h.t_s}, {"strength", h.strength}});
    }
    return out;
}

std::vector<std::uint8_t> frames_header(int pixels) {
    std::vector<std::uint8_t> out(kFramesMagic.begin(), kFramesMagic.end());
    put<std::uint32_t>(out, static_cast<std::uint32_t>(pixels));
    return out;
}

void append_frame(std::vector<std::uint8_t> &out, const RecordedFrame &f) {
    put<double>(out, f.t_s);
    put<std::int64_t>(out, f.index);
    put<std::uint64_t>(out, f.config_version);
    put<std::uint8_t>(out, f.flags);
    out.insert(out.end(), f.rgb.begin(), f.rgb.end());
}

Result<FramesFile> parse_frames(std::span<const std::uint8_t> in) {
    if (in.size() < kFramesHeaderBytes ||
        !std::equal(kFramesMagic.begin(), kFramesMagic.end(), in.begin(),
                    [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; })) {
        return Error{"not a frames file"};
    }
    FramesFile file;
    file.pixels = static_cast<int>(get<std::uint32_t>(in, 8));
    const size_t record = kRecordFixedBytes + 3 * static_cast<size_t>(file.pixels);
    size_t at = kFramesHeaderBytes;
    while (at + record <= in.size()) { // A truncated last record (a crash) is dropped.
        RecordedFrame f;
        f.t_s = get<double>(in, at);
        f.index = get<std::int64_t>(in, at + 8);
        f.config_version = get<std::uint64_t>(in, at + 16);
        f.flags = get<std::uint8_t>(in, at + 24);
        f.rgb.assign(in.begin() + static_cast<std::ptrdiff_t>(at + kRecordFixedBytes),
                     in.begin() + static_cast<std::ptrdiff_t>(at + record));
        file.frames.push_back(std::move(f));
        at += record;
    }
    return file;
}

Result<FramesFile> read_frames(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Error{"cannot open " + path_to_utf8(path)};
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    return parse_frames(bytes);
}

} // namespace psyight
