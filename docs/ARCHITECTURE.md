# AtlasEngine — Architecture Notes

Living notes on layering, known coupling, and where new code should go.
Build system: **premake5.lua only** (CMake was removed; deps come from
`scripts/fetch_deps.sh`, shaders from `scripts/build_shaders.*`).

## Layering (intended direction)

```text
editor / game_main
  -> ui / scene / scripting / physics / assets
    -> ecs / world / utils
      -> renderer / vulkan
        -> core / platform
```

Rules:

1. `src/ecs` must not include Vulkan or ImGui headers. Components hold plain
   data and resource **IDs**, never GPU handles.
2. `src/world` systems query ECS; they never own GPU resources.
3. `src/renderer` owns all `Vk*` objects and translates IDs to GPU resources.
4. `src/ui` and `src/editor` are the only places that include ImGui widgets.

## Known violations (do not extend)

- `src/ecs/ecs.h ::Mesh` still stores `VkBuffer`/`VkDeviceMemory` (marked
  DEPRECATED in-code), but no longer includes `<imgui.h>`: inspector UI moved
  to `src/ecs/inspector_ui.h` (only `ui/ui_manager.cpp` includes it).
  Handle wiring (Phase 2, done): every GPU mesh backing a `::Mesh` carries
  `Mesh::renderMeshId` from `Renderer::getMeshRegistry()`
  (`src/renderer/render_resources.h`):
  - allocate: the 4 ownership-transfer sites (editor primitive creation,
    editor model import, `game_main` primitive, city generator — the latter
    shares ONE handle across all boxes, freed by the first/owning entity);
  - free: `EditorApp::onMeshDestroyed` (central `on_destroy` hook, under the
    same `ownsGpuResources` guard as the deferred `vkDestroy` — mirror rule);
  - draw: picking/main/outline/instancing paths skip meshes whose handle is
    registered-but-dead; handle 0 = legacy/unregistered → drawn as before.
  - the standalone game never destroys meshes (no hook) → handles live until
    exit, mirroring the pre-existing Vk-buffer behavior.
  Remaining (Phase 3): make renderer draw/batching read the registry instead
  of `Mesh::Vk*`, then delete the deprecated fields.
- `src/ecs/components/components.h ::MeshComponent` duplicates `::Mesh`
  (also with `Vk*` handles). Unify on one mesh component during the
  decoupling above.
- `src/app/engine.h` declares `Atlas::Engine` with no `.cpp`; only
  `EditorLayer` holds an `Engine*`. Entry points use `EditorApp` directly.
  Either implement `Engine` or delete `app/` and retarget `editor.h`.
- `src/ecs/systems.h/.cpp` (empty stub) was deleted; systems live as
  `registry.view()` loops at call sites.
- `libs/atlas_ui` is not linked by any target that matters yet (only the
  premake `atlas_ui_lib` project references it). Decide: integrate or remove.

## Stubs with owners

- `src/world/culling.cpp` — Hi-Z occlusion returns "visible". Needs depth
  pyramid / software rasterizer.
- `src/world/world_partition.cpp:452` — streaming uses a frame counter;
  enqueue on `core/threading/thread_pool.h` instead.
- `src/vulkan/command_buffers.cpp:54` — pipeline bind/draw TODO.

New TODOs must reference an issue: `// TODO(#123): ...`.

## Build gotchas

- The premake-generated ninja files do NOT track header dependencies
  (no `-MD` in the compile rules, no `.d` files). After a **header-only**
  change, `touch` the affected `.cpp` files before `ninja`, or the exe
  silently links stale objects.

## Lighting & shadows (v1)

- Scene `LightComponent`s are uploaded to the GPU every frame
  (`Renderer::updateLightsAndShadow`): shadow-casting directional first
  (slot 0), then others up to 4. No lights → legacy hardcoded default.
- `cameraPos` is uploaded per frame (specular was computed from a stale value).
- Single 2048 directional shadow map: depth-only pass → comparison sampler
  (HW PCF 2x2) in `pbr_frag`. Fixed 80m ortho frustum around the camera
  target; toggle via `setShadowsEnabled` / inspector "Cast shadows".
- v1 limitations: no skinning in the depth pass (bind-pose shadows), no
  alpha-discard in depth (masked materials cast quad shadows), fixed
  frustum (upgrade: fit to view frustum / CSM), spot falls back to point.
- NOTE: `LightBuffer` layout is std140-sensitive — the `static_assert`s on
  `Light` (48B) / `LightBuffer` (304B) in `renderer.h` must match
  `pbr_frag.glsl` exactly; change both sides together.

## Threading

- Asset loading runs on workers (`core/threading/`). Shared registries
  touched from workers (e.g. `StringID`) must be internally synchronized.
- EnTT `registry` itself is **not** thread-safe: mutate only from one thread
  (or one entity at a time with group affinity).

## Tests

`tests/` builds as premake target `AtlasTests` (stdlib-only, no Vulkan):
`ninja -C build AtlasTests_Release && ./bin/Release/AtlasTests`.
Add a case file per module; keep engine headers Vulkan-free where tested.
