#include "gui/file_dialog.h"

#include <windows.h>

#include <shobjidl.h>

namespace psyight {

std::optional<std::filesystem::path> open_audio_file_dialog(void *owner) {
    // COM for this thread; S_FALSE (already initialised) is fine, and so is RPC_E_CHANGED_MODE
    // (initialised differently elsewhere): the dialog still works.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::optional<std::filesystem::path> chosen;
    IFileOpenDialog *dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog)))) {
        const COMDLG_FILTERSPEC filters[] = {{L"Audio (WAV, FLAC, MP3)", L"*.wav;*.flac;*.mp3"},
                                             {L"All files", L"*.*"}};
        dialog->SetFileTypes(2, filters);
        dialog->SetTitle(L"Open a track");
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
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

} // namespace psyight
