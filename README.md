# AtlasEngine

**A lightweight, modern, and performant game engine / rendering framework in development.**

![License](https://img.shields.io/github/license/AlexbavGamer/AtlasEngine)
![Language](https://img.shields.io/github/languages/top/AlexbavGamer/AtlasEngine)
![Stars](https://img.shields.io/github/stars/AlexbavGamer/AtlasEngine?style=social)

---

## About the Project

**AtlasEngine** is a game engine / rendering framework focused on **performance**, **simplicity**, and **modern C++ practices**.

The goal is to create a clean, flexible, and lightweight foundation for game prototypes, indie games, and technical experiments using current industry standards.

### Key Features

- **Language**: C++20 / C++23
- **Graphics API**: Vulkan (primary) • OpenGL • DirectX 12 (planned)
- **Platforms**: Windows, Linux (macOS planned)
- **Architecture**: Entity Component System (ECS)
- **Rendering**: Physically Based Rendering (PBR)
- **Ray Tracing**: Planned support
- **Asset Pipeline**: Modern and asynchronous
- **Editor**: In development
- **Scripting**: Lua / C# bindings (planned)

---

## Current Status

> **Phase**: Early Development / Prototyping

- [ ] Window and Vulkan context initialization
- [ ] Basic rendering pipeline
- [ ] Camera system
- [ ] glTF model loading
- [ ] ECS implementation
- [ ] Asset management system

---

## How to Build

### Prerequisites

- CMake 3.22 or higher
- C++20 compatible compiler (MSVC, GCC 11+, Clang 13+)
- Vulkan SDK (recommended)

### Build Instructions

```bash
git clone https://github.com/AlexbavGamer/AtlasEngine.git
cd AtlasEngine
git submodule update --init --recursive

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

---

## Project Structure

```
AtlasEngine/
├── src/                 # Source code
├── include/             # Public headers
├── assets/              # Sample models, textures, and shaders
├── external/            # Third-party dependencies (submodules)
├── CMakeLists.txt
├── docs/
└── tools/
```

---

## Roadmap

- [ ] MVP - Triangle with camera movement
- [ ] Asynchronous asset loading
- [ ] Full PBR + Image-Based Lighting
- [ ] ImGui Editor
- [ ] Lua scripting integration
- [ ] Physics system (Jolt / PhysX)
- [ ] Build & packaging for final games

---

## Contributing

Contributions are **welcome**!

1. Fork the project
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

---

## Acknowledgments

Inspired by great engines and frameworks:
- [Hazel](https://github.com/TheCherno/Hazel) by The Cherno
- [Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine)
- [Falcor](https://github.com/NVIDIAGameWorks/Falcor)

---

**Made with ❤️ by [Alexsandre](https://github.com/AlexbavGamer)**