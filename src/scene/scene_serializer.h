#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "scene.h"
#include "../scripting/script_types.h"

namespace Atlas {

struct SerializedScriptField {
    std::string name;
    Scripting::ScriptFieldValue value;
};

struct SerializedScriptComponent {
    bool enabled = true;
    std::string scriptPath;
    std::vector<SerializedScriptField> fields;
};

struct SerializedMaterialComponent {
    glm::vec4 baseColor{1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ambientOcclusion = 1.0f;
    glm::vec3 emissiveFactor{0.0f};
    uint32_t alphaMode = 0;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool invertCulling = false;

    bool useAlbedoTexture = false;
    std::string albedoTexturePath;
    bool useNormalTexture = false;
    std::string normalTexturePath;
    bool useMetallicRoughnessTexture = false;
    std::string metallicRoughnessTexturePath;
    bool useAOTexture = false;
    std::string aoTexturePath;
    bool useEmissiveTexture = false;
    std::string emissiveTexturePath;
};

struct SerializedEntity {
    uint32_t id = 0;
    int32_t parentId = -1;
    std::string name;

    bool hasTransform = false;
    Transform transform{};

    bool hasRenderable = false;
    Renderable renderable{};

    bool hasCamera = false;
    Camera camera{};

    bool hasEditorCamera = false;
    EditorCamera editorCamera{};

    bool hidden = false;

    bool hasFollowCamera = false;
    int32_t followTargetId = -1;
    glm::vec3 followOffset{0.0f, 2.0f, 5.0f};
    float followSmoothness = 8.0f;
    bool followLookAtTarget = true;

    bool hasGameCamera = false;
    bool gameCameraPrimary = true;

    bool hasRigidBody = false;
    ECS::RigidBodyComponent rigidBody{};

    bool hasBoxCollider = false;
    ECS::BoxColliderComponent boxCollider{};

    bool hasSphereCollider = false;
    ECS::SphereColliderComponent sphereCollider{};

    bool hasCapsuleCollider = false;
    ECS::CapsuleColliderComponent capsuleCollider{};

    bool hasMeshCollider = false;
    ECS::MeshColliderComponent meshCollider{};

    // Sun/Sky task: procedural sun + sky backdrop (persisted; LightComponent
    // itself is still scene-transient).
    bool hasSun = false;
    float sunAzimuthDeg = 135.0f;
    float sunElevationDeg = 50.0f;
    glm::vec3 sunColor = glm::vec3(1.0f, 0.96f, 0.90f);
    float sunIntensity = 3.0f;
    bool sunCastShadows = true;
    // v2+: shadow frustum half-extent (m). Absent in v1 files -> default.
    float sunShadowRange = 80.0f;

    bool hasSky = false;
    bool skyEnabled = true;
    glm::vec3 skyHorizon = glm::vec3(0.62f, 0.72f, 0.83f);
    glm::vec3 skyZenith = glm::vec3(0.19f, 0.36f, 0.63f);
    glm::vec3 skyGround = glm::vec3(0.09f, 0.09f, 0.11f);
    glm::vec3 skySunColor = glm::vec3(1.0f, 0.88f, 0.70f);
    float skySunDiskSizeDeg = 2.5f;
    float skySunGlow = 0.35f;

    std::vector<SerializedScriptComponent> scripts;

    std::string primitiveType;

    bool hasMaterial = false;
    SerializedMaterialComponent material{};
};

struct SerializedScene {
    std::string name = "Untitled";
    std::vector<SerializedEntity> entities;
};

class SceneSerializer {
public:
    static bool saveToFile(Scene& scene, const std::string& path);
    static bool loadFromFile(const std::string& path, SerializedScene& outScene);
};

} // namespace Atlas
