#include "io/track_library.h"

#include "core/paths.h"
#include "io/labels.h"

#include <algorithm>
#include <cwctype>
#include <set>

namespace psyight {

const char *label_status_name(LabelStatus status) {
    switch (status) {
    case LabelStatus::none:
        return "Not started";
    case LabelStatus::started:
        return "In progress";
    case LabelStatus::done:
        return "Done";
    }
    return "Not started";
}

LabelStatus label_status(const std::filesystem::path &audio, int *sections) {
    if (sections != nullptr) {
        *sections = 0;
    }
    const std::filesystem::path file = labels_path_for(audio);
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        return LabelStatus::none;
    }
    const Result<Labels> labels = load_labels(file);
    if (!labels) {
        return LabelStatus::started;
    }
    const Labels &l = labels.value();
    if (sections != nullptr) {
        *sections = l.sections_edited ? static_cast<int>(l.sections.size()) : 0;
    }
    if (l.sections_done) {
        return LabelStatus::done;
    }
    return (l.sections_edited && !l.sections.empty()) || l.phrases.set ? LabelStatus::started
                                                                       : LabelStatus::none;
}

namespace {

bool is_audio(const std::filesystem::path &path) {
    std::wstring ext = path.extension().wstring();
    for (wchar_t &c : ext) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return ext == L".wav" || ext == L".flac" || ext == L".mp3";
}

} // namespace

std::vector<TrackEntry> scan_track_folders(const std::vector<std::filesystem::path> &folders,
                                           int max_depth) {
    std::set<std::filesystem::path> seen;
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::path &folder : folders) {
        std::error_code ec;
        auto it = std::filesystem::recursive_directory_iterator(
            folder, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) {
            continue;
        }
        for (; it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            if (it->is_directory(ec) && it.depth() >= max_depth) {
                it.disable_recursion_pending();
                continue;
            }
            const std::filesystem::path path = it->path();
            if (it->is_regular_file(ec) && is_audio(path)) {
                const std::filesystem::path key = std::filesystem::weakly_canonical(path, ec);
                if (seen.insert(ec ? path : key).second) {
                    files.push_back(path);
                }
            }
        }
    }
    std::sort(files.begin(), files.end());
    std::vector<TrackEntry> tracks;
    for (const std::filesystem::path &path : files) {
        TrackEntry entry;
        entry.path = path;
        entry.name = path_to_utf8(path.filename());
        entry.folder = path_to_utf8(path.parent_path().filename());
        entry.status = label_status(path, &entry.sections);
        tracks.push_back(std::move(entry));
    }
    return tracks;
}

std::optional<size_t> next_unfinished(const std::vector<TrackEntry> &tracks, size_t from) {
    const size_t n = tracks.size();
    if (n == 0) {
        return std::nullopt;
    }
    const size_t start = from < n ? from + 1 : 0;
    for (size_t k = 0; k < n; ++k) {
        const size_t i = (start + k) % n;
        if (tracks[i].status != LabelStatus::done) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace psyight
