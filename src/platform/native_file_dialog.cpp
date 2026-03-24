#include "native_file_dialog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uuid.lib")
#endif

namespace Atlas::Platform {
namespace {

static std::string trimNewlines(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

#if !defined(_WIN32)
static std::string shellEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

static std::optional<std::string> runAndCapture(const std::string& cmd) {
#if defined(_WIN32)
    (void)cmd;
    return std::nullopt;
#else
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return std::nullopt;

    std::string result;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    int rc = pclose(pipe);
    if (rc != 0) {
        return std::nullopt;
    }
    result = trimNewlines(result);
    if (result.empty()) return std::nullopt;
    return result;
#endif
}

static bool hasCommand(const std::string& name) {
    std::string cmd = "command -v " + name + " >/dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    return rc == 0;
}

static std::string buildZenityFilters(const std::vector<FileDialogFilter>& filters) {
    std::string out;
    for (const auto& f : filters) {
        if (f.extensions.empty()) continue;
        std::ostringstream oss;
        oss << " --file-filter=" << shellEscape(f.description + " |");
        for (const auto& ext : f.extensions) {
            if (ext.empty()) continue;
            oss << " *." << ext;
        }
        out += oss.str();
    }
    return out;
}

static std::string buildKDialogFilter(const std::vector<FileDialogFilter>& filters) {
    // kdialog expects: "*.scene|Scene Files (*.scene)"
    if (filters.empty() || filters[0].extensions.empty()) {
        return std::string();
    }
    const auto& f = filters[0];
    std::ostringstream glob;
    for (size_t i = 0; i < f.extensions.size(); ++i) {
        if (i) glob << ' ';
        glob << "*." << f.extensions[i];
    }
    std::ostringstream oss;
    oss << shellEscape(glob.str() + "|" + f.description);
    return oss.str();
}
#endif

#if defined(_WIN32)
static std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring w;
    w.resize(static_cast<size_t>(len - 1));
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string s;
    s.resize(static_cast<size_t>(len - 1));
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
}

struct CoInit {
    HRESULT hr;
    CoInit() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~CoInit() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
};

static void applyFilters(IFileDialog* dlg, const std::vector<FileDialogFilter>& filters) {
    if (!dlg || filters.empty()) return;

    std::vector<COMDLG_FILTERSPEC> specs;
    std::vector<std::wstring> names;
    std::vector<std::wstring> patterns;

    specs.reserve(filters.size());
    names.reserve(filters.size());
    patterns.reserve(filters.size());

    for (const auto& f : filters) {
        if (f.extensions.empty()) continue;
        names.push_back(toWide(f.description.empty() ? std::string("Files") : f.description));
        std::wstring pat;
        for (size_t i = 0; i < f.extensions.size(); ++i) {
            if (i) pat += L";";
            pat += L"*.";
            pat += toWide(f.extensions[i]);
        }
        patterns.push_back(std::move(pat));
    }

    for (size_t i = 0; i < names.size(); ++i) {
        COMDLG_FILTERSPEC spec;
        spec.pszName = names[i].c_str();
        spec.pszSpec = patterns[i].c_str();
        specs.push_back(spec);
    }

    if (!specs.empty()) {
        dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }
}

static void applyDefaultFolder(IFileDialog* dlg, const std::string& defaultPath) {
    if (!dlg || defaultPath.empty()) return;

    std::wstring w = toWide(defaultPath);
    if (w.empty()) return;

    IShellItem* folder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(w.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder) {
        dlg->SetDefaultFolder(folder);
        folder->Release();
    }
}

static std::optional<std::string> showOpenDialog(bool folders,
                                                 const std::string& title,
                                                 const std::string& defaultPath,
                                                 const std::vector<FileDialogFilter>& filters) {
    CoInit co;
    if (FAILED(co.hr) && co.hr != RPC_E_CHANGED_MODE) {
        return std::nullopt;
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) {
        return std::nullopt;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM;
    if (folders) {
        options |= FOS_PICKFOLDERS;
    } else {
        options |= FOS_FILEMUSTEXIST;
    }
    dialog->SetOptions(options);

    dialog->SetTitle(toWide(title).c_str());
    applyDefaultFolder(dialog, defaultPath);
    if (!folders) {
        applyFilters(dialog, filters);
    }

    hr = dialog->Show(nullptr);
    if (FAILED(hr)) {
        dialog->Release();
        return std::nullopt;
    }

    IShellItem* item = nullptr;
    hr = dialog->GetResult(&item);
    if (FAILED(hr) || !item) {
        dialog->Release();
        return std::nullopt;
    }

    PWSTR path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    std::optional<std::string> out;
    if (SUCCEEDED(hr) && path) {
        out = toUtf8(path);
        CoTaskMemFree(path);
    }

    item->Release();
    dialog->Release();
    return out;
}

static std::optional<std::string> showSaveDialog(const std::string& title,
                                                 const std::string& defaultPath,
                                                 const std::vector<FileDialogFilter>& filters) {
    CoInit co;
    if (FAILED(co.hr) && co.hr != RPC_E_CHANGED_MODE) {
        return std::nullopt;
    }

    IFileSaveDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) {
        return std::nullopt;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM;
    dialog->SetOptions(options);

    dialog->SetTitle(toWide(title).c_str());
    applyDefaultFolder(dialog, defaultPath);
    applyFilters(dialog, filters);

    hr = dialog->Show(nullptr);
    if (FAILED(hr)) {
        dialog->Release();
        return std::nullopt;
    }

    IShellItem* item = nullptr;
    hr = dialog->GetResult(&item);
    if (FAILED(hr) || !item) {
        dialog->Release();
        return std::nullopt;
    }

    PWSTR path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    std::optional<std::string> out;
    if (SUCCEEDED(hr) && path) {
        out = toUtf8(path);
        CoTaskMemFree(path);
    }

    item->Release();
    dialog->Release();
    return out;
}
#endif

} // namespace

std::optional<std::string> openFileDialog(const std::string& title,
                                         const std::string& defaultPath,
                                         const std::vector<FileDialogFilter>& filters) {
#if defined(_WIN32)
    return showOpenDialog(false, title, defaultPath, filters);
#elif defined(__APPLE__)
    // AppleScript (best-effort)
    std::string cmd = "osascript -e " + shellEscape("POSIX path of (choose file with prompt \"" + title + "\")");
    return runAndCapture(cmd);
#else
    if (hasCommand("zenity")) {
        std::string cmd = "zenity --file-selection --title=" + shellEscape(title);
        if (!defaultPath.empty()) {
            cmd += " --filename=" + shellEscape(defaultPath + "/");
        }
        cmd += buildZenityFilters(filters);
        return runAndCapture(cmd);
    }
    if (hasCommand("kdialog")) {
        std::string filter = buildKDialogFilter(filters);
        std::string cmd = "kdialog --getopenfilename " + shellEscape(defaultPath.empty() ? std::string(".") : defaultPath);
        if (!filter.empty()) {
            cmd += " " + filter;
        }
        cmd += " --title " + shellEscape(title);
        return runAndCapture(cmd);
    }
    return std::nullopt;
#endif
}

std::optional<std::string> saveFileDialog(const std::string& title,
                                         const std::string& defaultPath,
                                         const std::vector<FileDialogFilter>& filters) {
#if defined(_WIN32)
    return showSaveDialog(title, defaultPath, filters);
#elif defined(__APPLE__)
    std::string cmd = "osascript -e " + shellEscape("POSIX path of (choose file name with prompt \"" + title + "\")");
    return runAndCapture(cmd);
#else
    if (hasCommand("zenity")) {
        std::string cmd = "zenity --file-selection --save --confirm-overwrite --title=" + shellEscape(title);
        if (!defaultPath.empty()) {
            cmd += " --filename=" + shellEscape(defaultPath + "/");
        }
        cmd += buildZenityFilters(filters);
        return runAndCapture(cmd);
    }
    if (hasCommand("kdialog")) {
        std::string filter = buildKDialogFilter(filters);
        std::string cmd = "kdialog --getsavefilename " + shellEscape(defaultPath.empty() ? std::string(".") : defaultPath);
        if (!filter.empty()) {
            cmd += " " + filter;
        }
        cmd += " --title " + shellEscape(title);
        return runAndCapture(cmd);
    }
    return std::nullopt;
#endif
}

std::optional<std::string> openFolderDialog(const std::string& title,
                                           const std::string& defaultPath) {
#if defined(_WIN32)
    return showOpenDialog(true, title, defaultPath, {});
#elif defined(__APPLE__)
    std::string cmd = "osascript -e " + shellEscape("POSIX path of (choose folder with prompt \"" + title + "\")");
    return runAndCapture(cmd);
#else
    if (hasCommand("zenity")) {
        std::string cmd = "zenity --file-selection --directory --title=" + shellEscape(title);
        if (!defaultPath.empty()) {
            cmd += " --filename=" + shellEscape(defaultPath + "/");
        }
        return runAndCapture(cmd);
    }
    if (hasCommand("kdialog")) {
        std::string cmd = "kdialog --getexistingdirectory " + shellEscape(defaultPath.empty() ? std::string(".") : defaultPath);
        cmd += " --title " + shellEscape(title);
        return runAndCapture(cmd);
    }
    return std::nullopt;
#endif
}

} // namespace Atlas::Platform
