#include <iostream>
#include <string>

#include "runtime_app.h"

int main(int argc, char** argv) {
    try {
        std::string packageRoot;
        if (argc > 1 && argv[1]) {
            packageRoot = argv[1];
        }
        Atlas::Runtime::RuntimeApp app(packageRoot);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
