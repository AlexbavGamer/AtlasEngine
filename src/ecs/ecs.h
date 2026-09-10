#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <memory>
#include <cstring>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <vector>
#include "../renderer/render_resources.h"
#include "../world/lod.h"
#include "reflection.h"

using Registry = entt::registry;
using Entity = entt::entity;

struct WorldTransform {
    glm::mat4 matrix{1.0f};
};

struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    Transform() = default;
    Transform(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& sc)
        : position(pos), rotation(rot), scale(sc) {}

    glm::mat4 getModelMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, scale);
        return model;
    }
};

struct Mesh {
    std::string meshPath;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    // GPU resources live in the renderer-side RenderResourceManager and are
    // referenced by this opaque handle (0 = none). Phase 3b complete:
    // the deprecated VkBuffer/VkDeviceMemory fields are gone, so this
    // header is Vulkan-free. Draw/batching paths resolve via
    // RenderResourceManager::getMeshData().
    Atlas::MeshHandle renderMeshId = Atlas::kInvalidMeshHandle;

    // True when the renderer has published GPU data for this mesh.
    // NOTE: liveness (not freed) is checked renderer-side via
    // RenderResourceManager::getMeshData(); this is only the "has backing"
    // gate for world systems that cannot touch GPU objects.
    bool hasGpuBacking() const { return renderMeshId != Atlas::kInvalidMeshHandle; }

    // Ownership of GPU buffers/memory. Cloned/runtime scenes should not free shared handles.
    bool ownsGpuResources = true;

    // Local-space bounds (computed on import when vertex data is available).
    bool hasBounds = false;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

struct WorldChunk {
    uint64_t cellKey = 0;
    bool isRoot = false;
};

struct Renderable {
    bool visible = true;
    uint32_t materialID = 0;
};

COMPONENT_FIELDS(Renderable,
    COMPONENT_FIELD(Renderable, visible, "Visible")
        .tooltip("Toggle rendering for this entity"),
    COMPONENT_FIELD(Renderable, materialID, "Material ID")
        .tooltip("Index into the renderer material palette")
        .read_only(true))

struct CameraBase {
    glm::vec3 position{0.0f, 0.0f, 5.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fov = 45.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, target, up);
    }

    glm::mat4 getProjectionMatrix() const {
        return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    }
};

struct Camera : CameraBase {};
struct EditorCamera : CameraBase {};

using World = entt::registry;

// NOTE: inspector UI (renderComponentProperties & friends) moved to
// ecs/inspector_ui.h so this header stays ImGui-free. Only editor UI code
// should include that header.

// (Inspector UI lives in ecs/inspector_ui.h — see NOTE above.)
