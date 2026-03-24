#include "scene_serializer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace Atlas {
namespace {
// Legacy text format magic
constexpr const char* kMagicText = "ATLAS_SCENE_V1";

// Binary format magic/version
constexpr uint8_t kMagicBin[] = {'A','T','L','A','S','_','S','C','N','_','B','I','N'};
constexpr uint32_t kBinVersion = 1;

std::string escapeString(const std::string& value) {
    std::ostringstream oss;
    oss << std::quoted(value);
    return oss.str();
}

bool readQuoted(std::istream& is, std::string& out) {
    is >> std::quoted(out);
    return !is.fail();
}

void writeFieldValueText(std::ostream& os, const Scripting::ScriptFieldValue& value) {
    using namespace Scripting;
    const ScriptFieldType type = getFieldType(value);
    os << getFieldTypeName(type) << ' ';
    switch (type) {
    case ScriptFieldType::Bool:
        os << (std::get<bool>(value) ? 1 : 0);
        break;
    case ScriptFieldType::Int:
        os << std::get<int>(value);
        break;
    case ScriptFieldType::Float:
        os << std::get<float>(value);
        break;
    case ScriptFieldType::String:
        os << escapeString(std::get<std::string>(value));
        break;
    case ScriptFieldType::Vec2: {
        const auto& v = std::get<glm::vec2>(value);
        os << v.x << ' ' << v.y;
        break;
    }
    case ScriptFieldType::Vec3: {
        const auto& v = std::get<glm::vec3>(value);
        os << v.x << ' ' << v.y << ' ' << v.z;
        break;
    }
    case ScriptFieldType::Vec4: {
        const auto& v = std::get<glm::vec4>(value);
        os << v.x << ' ' << v.y << ' ' << v.z << ' ' << v.w;
        break;
    }
    default:
        os << 0;
        break;
    }
}

bool readFieldValueText(std::istream& is, const std::string& typeName, Scripting::ScriptFieldValue& outValue) {
    using namespace Scripting;
    if (typeName == "bool") {
        int v = 0;
        is >> v;
        outValue = (v != 0);
        return !is.fail();
    }
    if (typeName == "int") {
        int v = 0;
        is >> v;
        outValue = v;
        return !is.fail();
    }
    if (typeName == "float") {
        float v = 0.0f;
        is >> v;
        outValue = v;
        return !is.fail();
    }
    if (typeName == "string") {
        std::string v;
        if (!readQuoted(is, v)) return false;
        outValue = v;
        return true;
    }
    if (typeName == "vec2") {
        glm::vec2 v(0.0f);
        is >> v.x >> v.y;
        outValue = v;
        return !is.fail();
    }
    if (typeName == "vec3") {
        glm::vec3 v(0.0f);
        is >> v.x >> v.y >> v.z;
        outValue = v;
        return !is.fail();
    }
    if (typeName == "vec4") {
        glm::vec4 v(0.0f);
        is >> v.x >> v.y >> v.z >> v.w;
        outValue = v;
        return !is.fail();
    }
    return false;
}

bool isPrimitiveMeshPath(const std::string& meshPath, std::string& outPrimitiveType) {
    constexpr const char* prefix = "primitive://";
    if (meshPath.rfind(prefix, 0) != 0) {
        return false;
    }
    outPrimitiveType = meshPath.substr(std::char_traits<char>::length(prefix));
    return !outPrimitiveType.empty();
}

SerializedMaterialComponent toSerializedMaterial(const ECS::MaterialComponent& material) {
    SerializedMaterialComponent out;
    out.baseColor = material.baseColor;
    out.metallic = material.metallic;
    out.roughness = material.roughness;
    out.ambientOcclusion = material.ambientOcclusion;
    out.emissiveFactor = material.emissiveFactor;
    out.alphaMode = static_cast<uint32_t>(material.alphaMode);
    out.alphaCutoff = material.alphaCutoff;
    out.doubleSided = material.doubleSided;
    out.invertCulling = material.invertCulling;
    out.useAlbedoTexture = material.useAlbedoTexture;
    out.albedoTexturePath = material.albedoTexturePath;
    out.useNormalTexture = material.useNormalTexture;
    out.normalTexturePath = material.normalTexturePath;
    out.useMetallicRoughnessTexture = material.useMetallicRoughnessTexture;
    out.metallicRoughnessTexturePath = material.metallicRoughnessTexturePath;
    out.useAOTexture = material.useAOTexture;
    out.aoTexturePath = material.aoTexturePath;
    out.useEmissiveTexture = material.useEmissiveTexture;
    out.emissiveTexturePath = material.emissiveTexturePath;
    return out;
}

// ---- Binary IO helpers (little endian) ----

template <typename T>
bool writePod(std::ostream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
    return !os.fail();
}

template <typename T>
bool readPod(std::istream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return !is.fail();
}

bool writeU8(std::ostream& os, uint8_t v) { return writePod(os, v); }
bool writeU32(std::ostream& os, uint32_t v) { return writePod(os, v); }
bool writeI32(std::ostream& os, int32_t v) { return writePod(os, v); }
bool writeU64(std::ostream& os, uint64_t v) { return writePod(os, v); }
bool writeF32(std::ostream& os, float v) { return writePod(os, v); }

bool readU8(std::istream& is, uint8_t& v) { return readPod(is, v); }
bool readU32(std::istream& is, uint32_t& v) { return readPod(is, v); }
bool readI32(std::istream& is, int32_t& v) { return readPod(is, v); }
bool readU64(std::istream& is, uint64_t& v) { return readPod(is, v); }
bool readF32(std::istream& is, float& v) { return readPod(is, v); }

bool writeVec2(std::ostream& os, const glm::vec2& v) {
    return writeF32(os, v.x) && writeF32(os, v.y);
}

bool writeVec3(std::ostream& os, const glm::vec3& v) {
    return writeF32(os, v.x) && writeF32(os, v.y) && writeF32(os, v.z);
}

bool writeVec4(std::ostream& os, const glm::vec4& v) {
    return writeF32(os, v.x) && writeF32(os, v.y) && writeF32(os, v.z) && writeF32(os, v.w);
}

bool readVec2(std::istream& is, glm::vec2& v) {
    return readF32(is, v.x) && readF32(is, v.y);
}

bool readVec3(std::istream& is, glm::vec3& v) {
    return readF32(is, v.x) && readF32(is, v.y) && readF32(is, v.z);
}

bool readVec4(std::istream& is, glm::vec4& v) {
    return readF32(is, v.x) && readF32(is, v.y) && readF32(is, v.z) && readF32(is, v.w);
}

bool writeString(std::ostream& os, const std::string& s) {
    if (s.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    const uint32_t len = static_cast<uint32_t>(s.size());
    if (!writeU32(os, len)) return false;
    if (len == 0) return true;
    os.write(s.data(), static_cast<std::streamsize>(len));
    return !os.fail();
}

bool readString(std::istream& is, std::string& out) {
    uint32_t len = 0;
    if (!readU32(is, len)) return false;
    out.clear();
    if (len == 0) return true;
    out.resize(len);
    is.read(out.data(), static_cast<std::streamsize>(len));
    return !is.fail();
}

enum EntityBinFlags : uint32_t {
    kHasTransform = 1u << 0,
    kHasRenderable = 1u << 1,
    kHasCamera = 1u << 2,
    kHasEditorCamera = 1u << 3,
    kIsHidden = 1u << 4,
    kHasFollowCamera = 1u << 5,
    kHasGameCamera = 1u << 6,
    kHasRigidBody = 1u << 7,
    kHasBoxCollider = 1u << 8,
    kHasSphereCollider = 1u << 9,
    kHasCapsuleCollider = 1u << 10,
    kHasPrimitive = 1u << 11,
    kHasMaterial = 1u << 12,
    kHasScripts = 1u << 13,
};

bool writeScriptFieldBin(std::ostream& os, const SerializedScriptField& field) {
    using namespace Scripting;
    if (!writeString(os, field.name)) return false;

    const ScriptFieldType type = getFieldType(field.value);
    if (!writeU8(os, static_cast<uint8_t>(type))) return false;

    switch (type) {
    case ScriptFieldType::Bool:
        return writeU8(os, std::get<bool>(field.value) ? 1 : 0);
    case ScriptFieldType::Int:
        return writeI32(os, static_cast<int32_t>(std::get<int>(field.value)));
    case ScriptFieldType::Float:
        return writeF32(os, std::get<float>(field.value));
    case ScriptFieldType::String:
        return writeString(os, std::get<std::string>(field.value));
    case ScriptFieldType::Vec2:
        return writeVec2(os, std::get<glm::vec2>(field.value));
    case ScriptFieldType::Vec3:
        return writeVec3(os, std::get<glm::vec3>(field.value));
    case ScriptFieldType::Vec4:
        return writeVec4(os, std::get<glm::vec4>(field.value));
    default:
        return false;
    }
}

bool readScriptFieldBin(std::istream& is, SerializedScriptField& outField) {
    using namespace Scripting;
    if (!readString(is, outField.name)) return false;

    uint8_t typeByte = 0;
    if (!readU8(is, typeByte)) return false;

    const ScriptFieldType type = static_cast<ScriptFieldType>(typeByte);
    switch (type) {
    case ScriptFieldType::Bool: {
        uint8_t v = 0;
        if (!readU8(is, v)) return false;
        outField.value = (v != 0);
        return true;
    }
    case ScriptFieldType::Int: {
        int32_t v = 0;
        if (!readI32(is, v)) return false;
        outField.value = static_cast<int>(v);
        return true;
    }
    case ScriptFieldType::Float: {
        float v = 0.0f;
        if (!readF32(is, v)) return false;
        outField.value = v;
        return true;
    }
    case ScriptFieldType::String: {
        std::string v;
        if (!readString(is, v)) return false;
        outField.value = std::move(v);
        return true;
    }
    case ScriptFieldType::Vec2: {
        glm::vec2 v(0.0f);
        if (!readVec2(is, v)) return false;
        outField.value = v;
        return true;
    }
    case ScriptFieldType::Vec3: {
        glm::vec3 v(0.0f);
        if (!readVec3(is, v)) return false;
        outField.value = v;
        return true;
    }
    case ScriptFieldType::Vec4: {
        glm::vec4 v(0.0f);
        if (!readVec4(is, v)) return false;
        outField.value = v;
        return true;
    }
    default:
        return false;
    }
}

bool saveToBinary(Scene& scene, std::ostream& out) {
    const auto entities = scene.getAllEntities();
    std::unordered_map<entt::entity, uint32_t> idMap;
    idMap.reserve(entities.size());
    for (uint32_t i = 0; i < entities.size(); ++i) {
        idMap[entities[i]] = i + 1;
    }

    const auto& registry = scene.getRegistry();

    // Header
    out.write(reinterpret_cast<const char*>(kMagicBin), sizeof(kMagicBin));
    if (!out.good()) return false;
    if (!writeU32(out, kBinVersion)) return false;
    if (!writeString(out, scene.getName())) return false;
    if (!writeU32(out, static_cast<uint32_t>(entities.size()))) return false;

    for (auto entity : entities) {
        const uint32_t id = idMap[entity];

        uint32_t flags = 0;
        if (registry.all_of<Transform>(entity)) flags |= kHasTransform;
        if (registry.all_of<Renderable>(entity)) flags |= kHasRenderable;
        if (registry.all_of<Camera>(entity)) flags |= kHasCamera;
        if (registry.all_of<EditorCamera>(entity)) flags |= kHasEditorCamera;
        if (registry.all_of<ECS::EditorHiddenComponent>(entity) && registry.get<ECS::EditorHiddenComponent>(entity).hidden) flags |= kIsHidden;
        if (registry.all_of<ECS::FollowCameraComponent>(entity)) flags |= kHasFollowCamera;
        if (registry.all_of<ECS::GameCameraComponent>(entity)) flags |= kHasGameCamera;
        if (registry.all_of<ECS::RigidBodyComponent>(entity)) flags |= kHasRigidBody;
        if (registry.all_of<ECS::BoxColliderComponent>(entity)) flags |= kHasBoxCollider;
        if (registry.all_of<ECS::SphereColliderComponent>(entity)) flags |= kHasSphereCollider;
        if (registry.all_of<ECS::CapsuleColliderComponent>(entity)) flags |= kHasCapsuleCollider;
        if (registry.all_of<ECS::MaterialComponent>(entity)) flags |= kHasMaterial;
        if (registry.all_of<ECS::ScriptComponent>(entity)) flags |= kHasScripts;

        std::string primitiveType;
        if (registry.all_of<::Mesh>(entity)) {
            const auto& mesh = registry.get<::Mesh>(entity);
            if (isPrimitiveMeshPath(mesh.meshPath, primitiveType)) {
                flags |= kHasPrimitive;
            }
        }

        // Entity header
        if (!writeU32(out, id)) return false;

        int32_t parentId = -1;
        if (registry.all_of<ECS::ParentComponent>(entity)) {
            const auto parent = registry.get<ECS::ParentComponent>(entity).parent;
            auto it = idMap.find(parent);
            if (it != idMap.end()) {
                parentId = static_cast<int32_t>(it->second);
            }
        }
        if (!writeI32(out, parentId)) return false;

        std::string tag = "Entity";
        if (registry.all_of<ECS::TagComponent>(entity)) {
            tag = registry.get<ECS::TagComponent>(entity).name;
        }
        if (!writeString(out, tag)) return false;

        if (!writeU32(out, flags)) return false;

        if (flags & kHasTransform) {
            const auto& t = registry.get<Transform>(entity);
            if (!writeVec3(out, t.position) || !writeVec3(out, t.rotation) || !writeVec3(out, t.scale)) return false;
        }

        if (flags & kHasRenderable) {
            const auto& r = registry.get<Renderable>(entity);
            if (!writeU8(out, r.visible ? 1 : 0)) return false;
            if (!writeU32(out, static_cast<uint32_t>(r.materialID))) return false;
        }

        if (flags & kHasCamera) {
            const auto& c = registry.get<Camera>(entity);
            if (!writeVec3(out, c.position) || !writeVec3(out, c.target) || !writeVec3(out, c.up)) return false;
            if (!writeF32(out, c.fov) || !writeF32(out, c.aspectRatio) || !writeF32(out, c.nearPlane) || !writeF32(out, c.farPlane)) return false;
        }

        if (flags & kHasEditorCamera) {
            const auto& c = registry.get<EditorCamera>(entity);
            if (!writeVec3(out, c.position) || !writeVec3(out, c.target) || !writeVec3(out, c.up)) return false;
            if (!writeF32(out, c.fov) || !writeF32(out, c.aspectRatio) || !writeF32(out, c.nearPlane) || !writeF32(out, c.farPlane)) return false;
        }

        if (flags & kHasFollowCamera) {
            const auto& f = registry.get<ECS::FollowCameraComponent>(entity);
            int32_t targetId = -1;
            auto it = idMap.find(f.target);
            if (it != idMap.end()) {
                targetId = static_cast<int32_t>(it->second);
            }
            if (!writeI32(out, targetId)) return false;
            if (!writeVec3(out, f.offset)) return false;
            if (!writeF32(out, f.smoothness)) return false;
            if (!writeU8(out, f.lookAtTarget ? 1 : 0)) return false;
        }

        if (flags & kHasGameCamera) {
            const auto& g = registry.get<ECS::GameCameraComponent>(entity);
            if (!writeU8(out, g.primary ? 1 : 0)) return false;
        }

        if (flags & kHasRigidBody) {
            const auto& rb = registry.get<ECS::RigidBodyComponent>(entity);
            if (!writeI32(out, static_cast<int32_t>(rb.motionType))) return false;
            if (!writeF32(out, rb.friction) || !writeF32(out, rb.restitution)) return false;
            if (!writeF32(out, rb.linearDamping) || !writeF32(out, rb.angularDamping)) return false;
            if (!writeF32(out, rb.gravityScale)) return false;
            if (!writeU8(out, rb.continuous ? 1 : 0)) return false;
            if (!writeU8(out, rb.allowSleep ? 1 : 0)) return false;
        }

        if (flags & kHasBoxCollider) {
            const auto& c = registry.get<ECS::BoxColliderComponent>(entity);
            if (!writeVec3(out, c.halfExtent)) return false;
            if (!writeVec3(out, c.offset)) return false;
            if (!writeU8(out, c.isTrigger ? 1 : 0)) return false;
        }

        if (flags & kHasSphereCollider) {
            const auto& c = registry.get<ECS::SphereColliderComponent>(entity);
            if (!writeF32(out, c.radius)) return false;
            if (!writeVec3(out, c.offset)) return false;
            if (!writeU8(out, c.isTrigger ? 1 : 0)) return false;
        }

        if (flags & kHasCapsuleCollider) {
            const auto& c = registry.get<ECS::CapsuleColliderComponent>(entity);
            if (!writeF32(out, c.radius) || !writeF32(out, c.halfHeight)) return false;
            if (!writeVec3(out, c.offset)) return false;
            if (!writeU8(out, c.isTrigger ? 1 : 0)) return false;
        }

        if (flags & kHasPrimitive) {
            if (!writeString(out, primitiveType)) return false;
        }

        if (flags & kHasMaterial) {
            const auto material = toSerializedMaterial(registry.get<ECS::MaterialComponent>(entity));
            if (!writeVec4(out, material.baseColor)) return false;
            if (!writeF32(out, material.metallic) || !writeF32(out, material.roughness) || !writeF32(out, material.ambientOcclusion)) return false;
            if (!writeVec3(out, material.emissiveFactor)) return false;
            if (!writeU32(out, material.alphaMode) || !writeF32(out, material.alphaCutoff)) return false;
            if (!writeU8(out, material.doubleSided ? 1 : 0) || !writeU8(out, material.invertCulling ? 1 : 0)) return false;

            if (!writeU8(out, material.useAlbedoTexture ? 1 : 0) || !writeString(out, material.albedoTexturePath)) return false;
            if (!writeU8(out, material.useNormalTexture ? 1 : 0) || !writeString(out, material.normalTexturePath)) return false;
            if (!writeU8(out, material.useMetallicRoughnessTexture ? 1 : 0) || !writeString(out, material.metallicRoughnessTexturePath)) return false;
            if (!writeU8(out, material.useAOTexture ? 1 : 0) || !writeString(out, material.aoTexturePath)) return false;
            if (!writeU8(out, material.useEmissiveTexture ? 1 : 0) || !writeString(out, material.emissiveTexturePath)) return false;
        }

        if (flags & kHasScripts) {
            const auto& scriptComponent = registry.get<ECS::ScriptComponent>(entity);
            if (scriptComponent.scripts.size() > std::numeric_limits<uint32_t>::max()) return false;
            if (!writeU32(out, static_cast<uint32_t>(scriptComponent.scripts.size()))) return false;

            for (const auto& script : scriptComponent.scripts) {
                if (!writeU8(out, script.enabled ? 1 : 0)) return false;
                if (!writeString(out, script.scriptPath)) return false;

                std::vector<std::string> fieldNames;
                fieldNames.reserve(script.fields.size());
                for (const auto& [name, _] : script.fields) {
                    fieldNames.push_back(name);
                }
                std::sort(fieldNames.begin(), fieldNames.end());

                if (fieldNames.size() > std::numeric_limits<uint32_t>::max()) return false;
                if (!writeU32(out, static_cast<uint32_t>(fieldNames.size()))) return false;

                for (const auto& fieldName : fieldNames) {
                    SerializedScriptField f;
                    f.name = fieldName;
                    f.value = script.fields.at(fieldName);
                    if (!writeScriptFieldBin(out, f)) return false;
                }
            }
        }
    }

    return !out.fail();
}

bool loadFromBinary(std::istream& in, SerializedScene& outScene) {
    uint32_t version = 0;
    if (!readU32(in, version) || version != kBinVersion) {
        return false;
    }

    outScene = {};
    if (!readString(in, outScene.name)) return false;

    uint32_t entityCount = 0;
    if (!readU32(in, entityCount)) return false;

    outScene.entities.clear();
    outScene.entities.reserve(entityCount);

    for (uint32_t i = 0; i < entityCount; ++i) {
        SerializedEntity e;
        if (!readU32(in, e.id)) return false;
        if (!readI32(in, e.parentId)) return false;
        if (!readString(in, e.name)) return false;

        uint32_t flags = 0;
        if (!readU32(in, flags)) return false;

        if (flags & kHasTransform) {
            e.hasTransform = true;
            if (!readVec3(in, e.transform.position) || !readVec3(in, e.transform.rotation) || !readVec3(in, e.transform.scale)) return false;
        }

        if (flags & kHasRenderable) {
            e.hasRenderable = true;
            uint8_t visible = 1;
            uint32_t materialID = 0;
            if (!readU8(in, visible) || !readU32(in, materialID)) return false;
            e.renderable.visible = (visible != 0);
            e.renderable.materialID = static_cast<int>(materialID);
        }

        if (flags & kHasCamera) {
            e.hasCamera = true;
            if (!readVec3(in, e.camera.position) || !readVec3(in, e.camera.target) || !readVec3(in, e.camera.up)) return false;
            if (!readF32(in, e.camera.fov) || !readF32(in, e.camera.aspectRatio) || !readF32(in, e.camera.nearPlane) || !readF32(in, e.camera.farPlane)) return false;
        }

        if (flags & kHasEditorCamera) {
            e.hasEditorCamera = true;
            if (!readVec3(in, e.editorCamera.position) || !readVec3(in, e.editorCamera.target) || !readVec3(in, e.editorCamera.up)) return false;
            if (!readF32(in, e.editorCamera.fov) || !readF32(in, e.editorCamera.aspectRatio) || !readF32(in, e.editorCamera.nearPlane) || !readF32(in, e.editorCamera.farPlane)) return false;
        }

        if (flags & kIsHidden) {
            e.hidden = true;
        }

        if (flags & kHasFollowCamera) {
            e.hasFollowCamera = true;
            uint8_t lookAt = 1;
            if (!readI32(in, e.followTargetId)) return false;
            if (!readVec3(in, e.followOffset)) return false;
            if (!readF32(in, e.followSmoothness)) return false;
            if (!readU8(in, lookAt)) return false;
            e.followLookAtTarget = (lookAt != 0);
        }

        if (flags & kHasGameCamera) {
            e.hasGameCamera = true;
            uint8_t primary = 1;
            if (!readU8(in, primary)) return false;
            e.gameCameraPrimary = (primary != 0);
        }

        if (flags & kHasRigidBody) {
            e.hasRigidBody = true;
            int32_t motionType = 0;
            uint8_t continuous = 0;
            uint8_t allowSleep = 1;
            if (!readI32(in, motionType)) return false;
            if (!readF32(in, e.rigidBody.friction) || !readF32(in, e.rigidBody.restitution)) return false;
            if (!readF32(in, e.rigidBody.linearDamping) || !readF32(in, e.rigidBody.angularDamping)) return false;
            if (!readF32(in, e.rigidBody.gravityScale)) return false;
            if (!readU8(in, continuous) || !readU8(in, allowSleep)) return false;
            e.rigidBody.motionType = static_cast<ECS::PhysicsMotionType>(motionType);
            e.rigidBody.continuous = (continuous != 0);
            e.rigidBody.allowSleep = (allowSleep != 0);
        }

        if (flags & kHasBoxCollider) {
            e.hasBoxCollider = true;
            uint8_t isTrigger = 0;
            if (!readVec3(in, e.boxCollider.halfExtent)) return false;
            if (!readVec3(in, e.boxCollider.offset)) return false;
            if (!readU8(in, isTrigger)) return false;
            e.boxCollider.isTrigger = (isTrigger != 0);
        }

        if (flags & kHasSphereCollider) {
            e.hasSphereCollider = true;
            uint8_t isTrigger = 0;
            if (!readF32(in, e.sphereCollider.radius)) return false;
            if (!readVec3(in, e.sphereCollider.offset)) return false;
            if (!readU8(in, isTrigger)) return false;
            e.sphereCollider.isTrigger = (isTrigger != 0);
        }

        if (flags & kHasCapsuleCollider) {
            e.hasCapsuleCollider = true;
            uint8_t isTrigger = 0;
            if (!readF32(in, e.capsuleCollider.radius) || !readF32(in, e.capsuleCollider.halfHeight)) return false;
            if (!readVec3(in, e.capsuleCollider.offset)) return false;
            if (!readU8(in, isTrigger)) return false;
            e.capsuleCollider.isTrigger = (isTrigger != 0);
        }

        if (flags & kHasPrimitive) {
            if (!readString(in, e.primitiveType)) return false;
        }

        if (flags & kHasMaterial) {
            e.hasMaterial = true;
            if (!readVec4(in, e.material.baseColor)) return false;
            if (!readF32(in, e.material.metallic) || !readF32(in, e.material.roughness) || !readF32(in, e.material.ambientOcclusion)) return false;
            if (!readVec3(in, e.material.emissiveFactor)) return false;
            if (!readU32(in, e.material.alphaMode) || !readF32(in, e.material.alphaCutoff)) return false;
            uint8_t doubleSided = 0;
            uint8_t invertCulling = 0;
            if (!readU8(in, doubleSided) || !readU8(in, invertCulling)) return false;
            e.material.doubleSided = (doubleSided != 0);
            e.material.invertCulling = (invertCulling != 0);

            uint8_t useAlbedoTexture = 0;
            uint8_t useNormalTexture = 0;
            uint8_t useMetallicRoughnessTexture = 0;
            uint8_t useAOTexture = 0;
            uint8_t useEmissiveTexture = 0;

            if (!readU8(in, useAlbedoTexture) || !readString(in, e.material.albedoTexturePath)) return false;
            if (!readU8(in, useNormalTexture) || !readString(in, e.material.normalTexturePath)) return false;
            if (!readU8(in, useMetallicRoughnessTexture) || !readString(in, e.material.metallicRoughnessTexturePath)) return false;
            if (!readU8(in, useAOTexture) || !readString(in, e.material.aoTexturePath)) return false;
            if (!readU8(in, useEmissiveTexture) || !readString(in, e.material.emissiveTexturePath)) return false;

            e.material.useAlbedoTexture = (useAlbedoTexture != 0);
            e.material.useNormalTexture = (useNormalTexture != 0);
            e.material.useMetallicRoughnessTexture = (useMetallicRoughnessTexture != 0);
            e.material.useAOTexture = (useAOTexture != 0);
            e.material.useEmissiveTexture = (useEmissiveTexture != 0);
        }

        if (flags & kHasScripts) {
            uint32_t scriptCount = 0;
            if (!readU32(in, scriptCount)) return false;

            e.scripts.clear();
            e.scripts.reserve(scriptCount);

            for (uint32_t si = 0; si < scriptCount; ++si) {
                SerializedScriptComponent script;
                uint8_t enabled = 1;
                if (!readU8(in, enabled)) return false;
                if (!readString(in, script.scriptPath)) return false;
                script.enabled = (enabled != 0);

                uint32_t fieldCount = 0;
                if (!readU32(in, fieldCount)) return false;

                script.fields.clear();
                script.fields.reserve(fieldCount);

                for (uint32_t fi = 0; fi < fieldCount; ++fi) {
                    SerializedScriptField field;
                    if (!readScriptFieldBin(in, field)) return false;
                    script.fields.push_back(std::move(field));
                }

                e.scripts.push_back(std::move(script));
            }
        }

        outScene.entities.push_back(std::move(e));
    }

    return !in.fail();
}

bool loadFromText(std::istream& in, SerializedScene& outScene) {
    std::string token;
    if (!(in >> token) || token != kMagicText) {
        return false;
    }

    outScene = {};
    SerializedEntity* current = nullptr;

    while (in >> token) {
        if (token == "scene_name") {
            if (!readQuoted(in, outScene.name)) return false;
        } else if (token == "entity_count") {
            size_t ignored = 0;
            in >> ignored;
        } else if (token == "entity") {
            outScene.entities.emplace_back();
            current = &outScene.entities.back();
            in >> current->id;
        } else if (token == "tag") {
            if (!current || !readQuoted(in, current->name)) return false;
        } else if (token == "parent") {
            if (!current) return false;
            in >> current->parentId;
        } else if (token == "transform") {
            if (!current) return false;
            current->hasTransform = true;
            in >> current->transform.position.x >> current->transform.position.y >> current->transform.position.z
               >> current->transform.rotation.x >> current->transform.rotation.y >> current->transform.rotation.z
               >> current->transform.scale.x >> current->transform.scale.y >> current->transform.scale.z;
        } else if (token == "renderable") {
            if (!current) return false;
            current->hasRenderable = true;
            int visible = 1;
            in >> visible >> current->renderable.materialID;
            current->renderable.visible = (visible != 0);
        } else if (token == "camera") {
            if (!current) return false;
            current->hasCamera = true;
            in >> current->camera.position.x >> current->camera.position.y >> current->camera.position.z
               >> current->camera.target.x >> current->camera.target.y >> current->camera.target.z
               >> current->camera.up.x >> current->camera.up.y >> current->camera.up.z
               >> current->camera.fov >> current->camera.aspectRatio >> current->camera.nearPlane >> current->camera.farPlane;
        } else if (token == "editor_camera") {
            if (!current) return false;
            current->hasEditorCamera = true;
            in >> current->editorCamera.position.x >> current->editorCamera.position.y >> current->editorCamera.position.z
               >> current->editorCamera.target.x >> current->editorCamera.target.y >> current->editorCamera.target.z
               >> current->editorCamera.up.x >> current->editorCamera.up.y >> current->editorCamera.up.z
               >> current->editorCamera.fov >> current->editorCamera.aspectRatio >> current->editorCamera.nearPlane >> current->editorCamera.farPlane;
        } else if (token == "hidden") {
            if (!current) return false;
            int hidden = 0;
            in >> hidden;
            current->hidden = (hidden != 0);
        } else if (token == "follow_camera") {
            if (!current) return false;
            current->hasFollowCamera = true;
            int lookAt = 1;
            in >> current->followTargetId
               >> current->followOffset.x >> current->followOffset.y >> current->followOffset.z
               >> current->followSmoothness >> lookAt;
            current->followLookAtTarget = (lookAt != 0);
        } else if (token == "game_camera") {
            if (!current) return false;
            current->hasGameCamera = true;
            int primary = 1;
            in >> primary;
            current->gameCameraPrimary = (primary != 0);
        } else if (token == "rigidbody") {
            if (!current) return false;
            current->hasRigidBody = true;
            int motionType = 0;
            int continuous = 0;
            int allowSleep = 1;
            in >> motionType
               >> current->rigidBody.friction >> current->rigidBody.restitution
               >> current->rigidBody.linearDamping >> current->rigidBody.angularDamping
               >> current->rigidBody.gravityScale
               >> continuous >> allowSleep;
            current->rigidBody.motionType = static_cast<ECS::PhysicsMotionType>(motionType);
            current->rigidBody.continuous = (continuous != 0);
            current->rigidBody.allowSleep = (allowSleep != 0);
        } else if (token == "box_collider") {
            if (!current) return false;
            current->hasBoxCollider = true;
            int isTrigger = 0;
            in >> current->boxCollider.halfExtent.x >> current->boxCollider.halfExtent.y >> current->boxCollider.halfExtent.z
               >> current->boxCollider.offset.x >> current->boxCollider.offset.y >> current->boxCollider.offset.z
               >> isTrigger;
            current->boxCollider.isTrigger = (isTrigger != 0);
        } else if (token == "sphere_collider") {
            if (!current) return false;
            current->hasSphereCollider = true;
            int isTrigger = 0;
            in >> current->sphereCollider.radius
               >> current->sphereCollider.offset.x >> current->sphereCollider.offset.y >> current->sphereCollider.offset.z
               >> isTrigger;
            current->sphereCollider.isTrigger = (isTrigger != 0);
        } else if (token == "capsule_collider") {
            if (!current) return false;
            current->hasCapsuleCollider = true;
            int isTrigger = 0;
            in >> current->capsuleCollider.radius >> current->capsuleCollider.halfHeight
               >> current->capsuleCollider.offset.x >> current->capsuleCollider.offset.y >> current->capsuleCollider.offset.z
               >> isTrigger;
            current->capsuleCollider.isTrigger = (isTrigger != 0);
        } else if (token == "primitive") {
            if (!current || !readQuoted(in, current->primitiveType)) return false;
        } else if (token == "material") {
            if (!current) return false;
            current->hasMaterial = true;
            int doubleSided = 0;
            int invertCulling = 0;
            int useAlbedoTexture = 0;
            int useNormalTexture = 0;
            int useMetallicRoughnessTexture = 0;
            int useAOTexture = 0;
            int useEmissiveTexture = 0;
            in >> current->material.baseColor.r >> current->material.baseColor.g >> current->material.baseColor.b >> current->material.baseColor.a
               >> current->material.metallic >> current->material.roughness >> current->material.ambientOcclusion
               >> current->material.emissiveFactor.x >> current->material.emissiveFactor.y >> current->material.emissiveFactor.z
               >> current->material.alphaMode >> current->material.alphaCutoff >> doubleSided >> invertCulling
               >> useAlbedoTexture;
            if (!readQuoted(in, current->material.albedoTexturePath)) return false;
            in >> useNormalTexture;
            if (!readQuoted(in, current->material.normalTexturePath)) return false;
            in >> useMetallicRoughnessTexture;
            if (!readQuoted(in, current->material.metallicRoughnessTexturePath)) return false;
            in >> useAOTexture;
            if (!readQuoted(in, current->material.aoTexturePath)) return false;
            in >> useEmissiveTexture;
            if (!readQuoted(in, current->material.emissiveTexturePath)) return false;
            current->material.doubleSided = (doubleSided != 0);
            current->material.invertCulling = (invertCulling != 0);
            current->material.useAlbedoTexture = (useAlbedoTexture != 0);
            current->material.useNormalTexture = (useNormalTexture != 0);
            current->material.useMetallicRoughnessTexture = (useMetallicRoughnessTexture != 0);
            current->material.useAOTexture = (useAOTexture != 0);
            current->material.useEmissiveTexture = (useEmissiveTexture != 0);
        } else if (token == "script") {
            if (!current) return false;
            SerializedScriptComponent script;
            int enabled = 1;
            size_t fieldCount = 0;
            in >> enabled;
            if (!readQuoted(in, script.scriptPath)) return false;
            in >> fieldCount;
            script.enabled = (enabled != 0);
            script.fields.clear();
            script.fields.reserve(fieldCount);
            for (size_t i = 0; i < fieldCount; ++i) {
                std::string fieldToken;
                in >> fieldToken;
                if (fieldToken != "field") return false;
                SerializedScriptField field;
                std::string typeName;
                if (!readQuoted(in, field.name)) return false;
                in >> typeName;
                if (!readFieldValueText(in, typeName, field.value)) return false;
                script.fields.push_back(std::move(field));
            }
            std::string endToken;
            in >> endToken;
            if (endToken != "endscript") return false;
            current->scripts.push_back(std::move(script));
        } else if (token == "endentity") {
            current = nullptr;
        } else {
            return false;
        }

        if (in.fail()) {
            return false;
        }
    }

    return true;
}

} // namespace

bool SceneSerializer::saveToFile(Scene& scene, const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    return saveToBinary(scene, out);
}

bool SceneSerializer::loadFromFile(const std::string& path, SerializedScene& outScene) {
    // Detect format
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            return false;
        }

        uint8_t magic[sizeof(kMagicBin)] = {};
        in.read(reinterpret_cast<char*>(magic), sizeof(magic));
        if (in.gcount() == static_cast<std::streamsize>(sizeof(magic)) &&
            std::memcmp(magic, kMagicBin, sizeof(kMagicBin)) == 0) {
            return loadFromBinary(in, outScene);
        }
    }

    // Try legacy text
    {
        std::ifstream in(path);
        if (!in.is_open()) {
            return false;
        }
        return loadFromText(in, outScene);
    }
}

} // namespace Atlas
