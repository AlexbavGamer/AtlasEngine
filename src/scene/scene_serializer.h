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
