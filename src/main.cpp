#include <iostream>
#include <string>

#include "editor/editor_app.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// Fatal errors must be visible even when the app is launched by double-click
// (no console attached): log to stderr AND pop a native message box.
void showFatalError(const std::string& message) {
    std::cerr << message << std::endl;
#ifdef _WIN32
    MessageBoxA(nullptr, message.c_str(), "AtlasEngine - Fatal Error",
                MB_OK | MB_ICONERROR);
#endif
}

} // namespace

int main() {
    try {
        Atlas::EditorApp editor;
        editor.run();
    } catch (const std::exception& e) {
        showFatalError(std::string("AtlasEngine failed to start:\n") + e.what());
        return EXIT_FAILURE;
    } catch (...) {
        showFatalError("AtlasEngine failed to start (unknown, non-std exception).");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
