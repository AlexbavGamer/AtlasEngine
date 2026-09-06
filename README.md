# AtlasEngine

**A lightweight, modern, and performant game engine / rendering framework in development.**

![License](https://img.shields.io/github/license/AlexbavGamer/AtlasEngine)
![Language](https://img.shields.io/github/languages/top/AlexbavGamer/AtlasEngine)
![Stars](https://img.shields.io/github/stars/AlexbavGamer/AtlasEngine?style=social)

---

## About the Project

**AtlasEngine** is a game engine / rendering framework focused on **performance**,
**simplicity**, and **modern C++ practices**.

The goal is a clean, flexible, and lightweight foundation for game prototypes,
indie games, and technical experiments using current industry standards.

### Key Features

- **Language**: C++17
- **Graphics API**: Vulkan
- **Platforms**: Windows, Linux (macOS planned)
- **Architecture**: Entity Component System (ECS via EnTT)
- **Rendering**: Physically Based Rendering (PBR) + instancing, HLOD, frustum culling
- **Asset Pipeline**: glTF/FBX/OBJ/DAE via Assimp, embedded textures, SPIR-V embedded in the exe
- **Editor**: ImGui dockable editor with play mode, gizmos, scene serialization
- **Scripting**: Lua runtime with per-entity scripts and inspector fields
- **Physics**: Custom CPU physics (static/dynamic/kinematic, box/sphere/capsule/mesh colliders)
- **Export**: Standalone game builder (Windows + Linux) with package manifest

---

## Current Status

> **Phase**: Editor + playable games working; hardening and decoupling in progress.

- [x] Window and Vulkan context initialization
- [x] PBR rendering pipeline with picking and outline passes
- [x] Camera system (editor + game cameras)
- [x] glTF/FBX model loading with embedded textures
- [x] ECS implementation (EnTT)
- [x] Asset management system
- [x] Lua scripting integration
- [x] Custom physics system
- [x] Editor with play mode + game export/packaging
- [ ] ECS/GPU decoupling (`Mesh` still holds `VkBuffer` handles — see `docs/ARCHITECTURE.md`)
- [ ] Hi-Z occlusion (culling stub returns "visible")
- [ ] Async world-partition streaming (uses a frame counter, not the thread pool yet)

---

## How to Build

### Prerequisites

- [premake5](https://premake.github.io) (5.0.0-beta2 or newer)
- [Ninja](https://ninja-build.org)
- C++17 compiler: MinGW-GCC on Windows (`g++`), GCC/Clang on Linux
- [Vulkan SDK](https://vulkan.lunarg.com) (provides `glslc` + `VULKAN_SDK`)
- Linux only: X11/Wayland dev headers (`xorg-dev libwayland-dev libxkbcommon-dev ...`)
- Git Bash (Windows) for the helper scripts

### Build Instructions

```bash
git clone https://github.com/AlexbavGamer/AtlasEngine.git
cd AtlasEngine

# 1. Fetch third-party sources into deps/src (pinned versions, idempotent)
./scripts/fetch_deps.sh

# 2. Generate Ninja files (also compiles + embeds shaders on Windows)
premake5 ninja

# 3. Build the editor
ninja -C build AtlasEngine_Release

# 4. Run
./bin/Release/AtlasEngine
```

Linux shader notes: run `./scripts/build_shaders.sh` once before generating
(it needs `glslc` on `PATH`; embedding falls back to `python3` when `powershell`
is unavailable).

### Unit tests (stdlib-only, no Vulkan)

```bash
ninja -C build AtlasTests_Release && ./bin/Release/AtlasTests
```

Premake targets: `AtlasEngine` (editor), `AtlasTests` (unit tests),
`glfw`, `imgui_lib`, `imguizmo_lib`, `lua_lib`, `assimp`, `zlib`,
`TracyClient`, `atlas_ui_lib`. Game binaries are produced by
`scripts/build_linux.sh` / the editor's Export menu, not by a premake target.

---

## Project Structure

```
AtlasEngine/
├── src/                 # Engine source
│   ├── animation/       # Skeleton + clip player
│   ├── app/             # Engine bootstrap (legacy, see ARCHITECTURE.md)
│   ├── assets/          # AssetManager (async loading, StringID registry)
│   ├── core/            # Profiler, threading, StringID, memory
│   ├── ecs/             # Components (EnTT), reflection for inspector UI
│   ├── editor/          # EditorApp, viewport, play mode
│   ├── export/          # Package manifest + game builder
│   ├── physics/         # Custom physics engine
│   ├── platform/        # Window (GLFW), native file dialogs
│   ├── project/         # Project manager (.atlasproject)
│   ├── renderer/        # Vulkan renderer (PBR, picking, instancing)
│   ├── scene/           # Scene + binary/text serializers
│   ├── scripting/       # Lua ScriptEngine
│   ├── ui/              # Editor UI panels (ImGui)
│   ├── utils/           # Model loader, frustum, LOD helpers, camera
│   ├── vulkan/          # Instance/device/swapchain/pass wrappers
│   └── world/           # Partitioning, HLOD, culling, occlusion, city gen
├── libs/atlas_ui/       # In-house retained-mode UI lib (experimental)
├── shaders/             # GLSL sources (compiled + embedded at build time)
├── scripts/             # fetch_deps, shader build/embed, game packaging
├── tests/               # AtlasTests unit tests
├── docs/                # Design docs + ARCHITECTURE.md
├── game/                # Default game project data
└── premake5.lua         # Sole build definition (CMake was removed)
```

---

## Roadmap

- [ ] Decouple ECS from Vulkan (ID-based `Mesh`/`Material` handles, render resource manager)
- [ ] Split god files (`renderer.cpp`, `ui_manager.cpp`, `editor_app.cpp`, `model_loader.h`)
- [ ] Real Hi-Z occlusion + async partition streaming via `ThreadPool`
- [ ] Integrate or remove `libs/atlas_ui` (ImGui is the editor UI today)
- [ ] Full PBR + Image-Based Lighting
- [ ] Ray tracing support
- [ ] C# scripting bindings

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
