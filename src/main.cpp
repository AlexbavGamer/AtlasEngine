#include <iostream>

#include "editor/editor_app.h"

int main() {
    try {
        Atlas::EditorApp editor;
        editor.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}