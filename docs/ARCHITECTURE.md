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

- `src/ecs/ecs.h ::Mesh` is Vulkan-free (Phase 3b, done): GPU handles live
  only in the renderer-side `RenderResourceManager` as opaque `MeshHandle`s.
  Inspector UI lives in `src/ecs/inspector_ui.h` (only `ui/ui_manager.cpp`
  includes it). Handle wiring:
  - allocate + publish: the 4 ownership-transfer sites (editor primitive
    creation, editor model import, `game_main` primitive, city generator —
    the latter shares ONE handle across all boxes, freed by the first/owning
    entity) call `allocateMesh()` + `setMeshData()` back-to-back;
  - free: `EditorApp::onMeshDestroyed` (central `on_destroy` hook, under the
    same `ownsGpuResources` guard as before) resolves the binding via
    `getMeshData()` for the deferred `vkDestroy`, then `freeMesh()` (which
    also clears the slot binding so dead slots never resolve);
  - draw: ALL renderer paths (shadow, picking, main + instancing batch keys,
    outline, auto-LOD resolve) bind buffers via `resolveMeshDrawBuffers()`
    → `getMeshData()`; there is no `Mesh::Vk*` fallback anymore.
  - world systems (`lod.cpp`, `hlod.cpp`) gate on `Mesh::hasGpuBacking()`
    and group by `renderMeshId` — never touching `Vk*` objects.
  - the standalone game never destroys meshes (no hook) → handles live until
    exit, mirroring the pre-existing Vk-buffer behavior.
  - serializer: no migration needed — only `primitiveType`/`meshPath` are
    persisted; GPU buffers are recreated on load through the same
    allocate+publish sites. Covered by `tests/test_render_resources.cpp`
    (publish/resolve, stale isolation).
- `src/ecs/components/components.h ::MeshComponent` was deleted (dead duplicate
  of `::Mesh` — never instantiated; only `SkinnedMeshComponent` remains).
  The header no longer includes `<vulkan/vulkan.h>`.
- `src/ecs/vertex.h` moved to `src/renderer/vertex.h` (only renderer/ and
  utils/ ever included it). `src/ecs` is now free of vertex-layout knowledge.
- `src/world/city_generator.h` no longer includes `<vulkan/vulkan.h>`:
  `cacheImportLODs` takes opaque `uint64_t` GPU-buffer keys; the bit-cast
  back to `VkBuffer` happens only in `city_generator.cpp` at the renderer
  boundary.
- `ECS::LightComponent::type` is now the `Type` enum (was `uint32_t` on one
  side of a bad rebase merge that broke the build); renderer, inspector and
  creation sites agree again.
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

## Lighting, sun & sky

- Scene `LightComponent`s are uploaded to the GPU every frame
  (`Renderer::updateLightsAndShadow`): shadow-casting directional first
  (slot 0), then others up to 4. No lights → legacy hardcoded default.
- **Sun** (`ECS::SunComponent`, `components.h`): procedural directional light
  driven by azimuth/elevation (no Transform needed). Caster priority (the
  frag shadows slot 0 only, so slot 0 MUST be the caster): casting sun
  first, then the first cast-flagged **directional** `LightComponent` (a sun
  that doesn't cast yields its slot to one that does), then an unshadowed
  sun as key light. Shadows only ever come from components — no phantom
  shadow without a caster. `setTimeOfDay(h)` runs a full 24h cycle (6h sunrise, 12h peak, 18h sunset, 0h/24h nadir 65 deg below the
  horizon). Below the horizon the renderer fades intensity to 0 and drops
  the shadow caster (night = ambient only). Created via the toolbar
  "Sun & Sky" menu or Add Component > Sun; persisted by the serializer
  (`kHasSun`).
- **Sky** (`ECS::SkyComponent`): procedural gradient (horizon/zenith/ground)
  - sun disk + halo, drawn as a **fullscreen triangle** (`gl_VertexIndex`, no
  buffers) at the far plane FIRST in the main pass (editor + game views),
  depth test `LEQUAL`, depth writes off, no descriptor sets — everything via
  `SkyPushConstants` (pinned by `offsetof` asserts in `renderer.h`, same
  discipline as `PushConstants` after the pbr_vert layout bug). The gradient
  fades to deep blue-black with the sun (`dayness` smoothstep) while the
  disk keeps its own below-horizon fade, so sunset lingers then night falls. No enabled
  sky = legacy clear-color background. The disk tracks the scene sun (or
  zenith default). `createSkyPipeline()` joins the recreate path alongside
  the other pipelines. Persisted via `kHasSky`; old scene files load with
  component defaults.

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
- Depth-range gotcha (no `GLM_FORCE_DEPTH_ZERO_TO_ONE` in this project):
  `glm::ortho`/`glm::perspective` map depth to OpenGL `[-1,1]`, but Vulkan
  clips `z<0`. The main pass survives by luck (its negative range covers
  only a sub-near sliver); the shadow ortho needed an explicit remap to
  `[0,1]` (`proj[2][2]*=0.5; proj[3][2]*0.5+0.5`) — without it the whole
  depth map came out empty. Touch this code only with a GPU readback.
- Quality (v1 polish): 5x5 manual PCF + slope-scaled bias + receiver normal
  offset; `SunComponent::shadowRange` drives the frustum half-extent live
  (smaller = sharper, less coverage). Scene format is v2 (range appended
  to the sun block; v1 files load with the default).
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
