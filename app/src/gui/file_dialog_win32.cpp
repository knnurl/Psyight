#include "gui/file_dialog.h"

#include <windows.h>

#include <shobjidl.h>

#include <chrono>
#include <future>
#include <thread>

namespace psyight {

namespace {

struct DialogSpec {
    const COMDLG_FILTERSPEC *filters;
    UINT filter_count;
    const wchar_t *title;
    const wchar_t *default_extension; // Save: appended when the name has none.
    std::filesystem::path suggested;  // Save: the starting name and folder.
    bool folder = false;              // Open: pick a folder instead of a file.
};

// Shows the open (FileOpenDialog) or save (FileSaveDialog) dialog and returns the chosen path.
std::optional<std::filesystem::path> show_dialog(void *owner, bool save, const DialogSpec &spec) {
    // COM for this thread; S_FALSE (already initialised) is fine, and so is RPC_E_CHANGED_MODE
    // (initialised differently elsewhere): the dialog still works.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (com == RPC_E_CHANGED_MODE) {
        // This thread is multithreaded (COM): the dialog would hang here. It runs on a thread of
        // its own (single-threaded), while this one handles its messages: the dialog disables its
        // owner, a window of this thread, and waits for the answer.
        std::promise<std::optional<std::filesystem::path>> promise;
        std::future<std::optional<std::filesystem::path>> result = promise.get_future();
        std::thread worker([&] { promise.set_value(show_dialog(owner, save, spec)); });
        bool quit = false;
        WPARAM quit_code = 0;
        while (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    quit = true; // Kept for the app's own loop.
                    quit_code = message.wParam;
                    continue;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        worker.join();
        if (quit) {
            PostQuitMessage(static_cast<int>(quit_code));
        }
        return result.get();
    }
    std::optional<std::filesystem::path> chosen;
    IFileDialog *dialog = nullptr;
    const HRESULT made = save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                                                 CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
                              : CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                                 CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(made)) {
        if (!spec.folder) {
            dialog->SetFileTypes(spec.filter_count, spec.filters);
        }
        dialog->SetTitle(spec.title);
        DWORD options = 0;
        dialog->GetOptions(&options);
        options |= FOS_FORCEFILESYSTEM;
        if (spec.folder) {
            options |= FOS_PICKFOLDERS;
        }
        options |= save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST;
        dialog->SetOptions(options);
        if (spec.default_extension != nullptr) {
            dialog->SetDefaultExtension(spec.default_extension);
        }
        if (!spec.suggested.empty()) {
            dialog->SetFileName(spec.suggested.filename().c_str());
            const std::filesystem::path folder =
                std::filesystem::absolute(spec.suggested).parent_path();
            IShellItem *item = nullptr;
            if (SUCCEEDED(
                    SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
                dialog->SetFolder(item);
                item->Release();
            }
        }
        if (SUCCEEDED(dialog->Show(static_cast<HWND>(owner)))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    chosen = std::filesystem::path(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (com == S_OK || com == S_FALSE) {
        CoUninitialize();
    }
    return chosen;
}

const COMDLG_FILTERSPEC kProjectFilters[] = {{L"Psyight project", L"*.psyproj"},
                                             {L"All files", L"*.*"}};

} // namespace

std::optional<std::filesystem::path> open_audio_file_dialog(void *owner) {
    const COMDLG_FILTERSPEC filters[] = {{L"Audio (WAV, FLAC, MP3)", L"*.wav;*.flac;*.mp3"},
                                         {L"All files", L"*.*"}};
    return show_dialog(owner, false, {filters, 2, L"Open a track", nullptr, {}});
}

std::optional<std::filesystem::path> open_project_file_dialog(void *owner) {
    return show_dialog(owner, false, {kProjectFilters, 2, L"Open a project", nullptr, {}});
}

std::optional<std::filesystem::path> open_svg_file_dialog(void *owner) {
    const COMDLG_FILTERSPEC filters[] = {{L"Drawings (SVG, DXF)", L"*.svg;*.dxf"},
                                         {L"All files", L"*.*"}};
    return show_dialog(owner, false, {filters, 2, L"Make a rig from a drawing", nullptr, {}});
}

std::optional<std::filesystem::path> open_rekordbox_file_dialog(void *owner) {
    const COMDLG_FILTERSPEC filters[] = {{L"rekordbox collection (XML)", L"*.xml"},
                                         {L"All files", L"*.*"}};
    return show_dialog(owner, false,
                       {filters, 2, L"The rekordbox collection exported as XML", nullptr, {}});
}

std::optional<std::filesystem::path>
save_drawing_file_dialog(void *owner, const std::filesystem::path &suggested, bool dxf) {
    const COMDLG_FILTERSPEC svg_filters[] = {{L"SVG drawing", L"*.svg"}, {L"All files", L"*.*"}};
    const COMDLG_FILTERSPEC dxf_filters[] = {{L"DXF drawing", L"*.dxf"}, {L"All files", L"*.*"}};
    std::optional<std::filesystem::path> chosen =
        show_dialog(owner, true,
                    {dxf ? dxf_filters : svg_filters, 2, L"Export the rig as a drawing",
                     dxf ? L"dxf" : L"svg", suggested});
    if (chosen && chosen->extension().empty()) {
        *chosen += dxf ? ".dxf" : ".svg";
    }
    return chosen;
}

std::optional<std::filesystem::path> pick_folder_dialog(void *owner) {
    return show_dialog(owner, false, {nullptr, 0, L"Add a music folder", nullptr, {}, true});
}

std::optional<std::filesystem::path>
save_project_file_dialog(void *owner, const std::filesystem::path &suggested) {
    std::optional<std::filesystem::path> chosen = show_dialog(
        owner, true, {kProjectFilters, 2, L"Save the project as", L"psyproj", suggested});
    if (chosen && chosen->extension() != ".psyproj") {
        *chosen += ".psyproj"; // "All files" chosen and no extension typed.
    }
    return chosen;
}

} // namespace psyight
