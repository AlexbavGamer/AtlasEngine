#include "scene_serializer.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace Atlas {
namespace {
constexpr const char* kMagic = "ATLAS_SCENE_V1";

std::string escapeString(const std::string& value) {
    std::ostringstream oss;
    oss << std::quoted(value);
    return oss.str();
}

bool readQuoted(std::istream& is, std::string& out) {
    is >> std::quoted(out);
    return !is.fail();
}

void writeFieldValue(std::ostream& os, const Scripting::ScriptFieldValue& value) {
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

bool readFieldValue(std::istream& is, const std::string& typeName, Scripting::ScriptFieldValue& outValue) {
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
    return out;
}

} // namespace

bool SceneSerializer::saveToFile(Scene& scene, const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    const auto entities = scene.getAllEntities();
    std::unordered_map<entt::entity, uint32_t> idMap;
    idMap.reserve(entities.size());
    for (uint32_t i = 0; i < entities.size(); ++i) {
        idMap[entities[i]] = i + 1;
    }

    const auto& registry = scene.getRegistry();

    out << kMagic << '\n';
    out << "scene_name " << escapeString(scene.getName()) << '\n';
    out << "entity_count " << entities.size() << '\n';

    for (auto entity : entities) {
        const uint32_t id = idMap[entity];
        out << "entity " << id << '\n';

        std::string tag = "Entity";
        if (registry.all_of<ECS::TagComponent>(entity)) {
            tag = registry.get<ECS::TagComponent>(entity).name;
        }
        out << "tag " << escapeString(tag) << '\n';

        int32_t parentId = -1;
        if (registry.all_of<ECS::ParentComponent>(entity)) {
            const auto parent = registry.get<ECS::ParentComponent>(entity).parent;
            auto it = idMap.find(parent);
            if (it != idMap.end()) {
                parentId = static_cast<int32_t>(it->second);
            }
        }
        out << "parent " << parentId << '\n';

        if (registry.all_of<Transform>(entity)) {
            const auto& t = registry.get<Transform>(entity);
            out << "transform "
                << t.position.x << ' ' << t.position.y << ' ' << t.position.z << ' '
                << t.rotation.x << ' ' << t.rotation.y << ' ' << t.rotation.z << ' '
                << t.scale.x << ' ' << t.scale.y << ' ' << t.scale.z << '\n';
        }

        if (registry.all_of<Renderable>(entity)) {
            const auto& r = registry.get<Renderable>(entity);
            out << "renderable " << (r.visible ? 1 : 0) << ' ' << r.materialID << '\n';
        }

        if (registry.all_of<Camera>(entity)) {
            const auto& c = registry.get<Camera>(entity);
            out << "camera "
                << c.position.x << ' ' << c.position.y << ' ' << c.position.z << ' '
                << c.target.x << ' ' << c.target.y << ' ' << c.target.z << ' '
                << c.up.x << ' ' << c.up.y << ' ' << c.up.z << ' '
                << c.fov << ' ' << c.aspectRatio << ' ' << c.nearPlane << ' ' << c.farPlane << '\n';
        }

        if (registry.all_of<EditorCamera>(entity)) {
            const auto& c = registry.get<EditorCamera>(entity);
            out << "editor_camera "
                << c.position.x << ' ' << c.position.y << ' ' << c.position.z << ' '
                << c.target.x << ' ' << c.target.y << ' ' << c.target.z << ' '
                << c.up.x << ' ' << c.up.y << ' ' << c.up.z << ' '
                << c.fov << ' ' << c.aspectRatio << ' ' << c.nearPlane << ' ' << c.farPlane << '\n';
        }

        if (registry.all_of<ECS::EditorHiddenComponent>(entity)) {
            const auto& h = registry.get<ECS::EditorHiddenComponent>(entity);
            out << "hidden " << (h.hidden ? 1 : 0) << '\n';
        }

        if (registry.all_of<ECS::FollowCameraComponent>(entity)) {
            const auto& f = registry.get<ECS::FollowCameraComponent>(entity);
            int32_t targetId = -1;
            auto it = idMap.find(f.target);
            if (it != idMap.end()) {
                targetId = static_cast<int32_t>(it->second);
            }
            out << "follow_camera " << targetId << ' '
                << f.offset.x << ' ' << f.offset.y << ' ' << f.offset.z << ' '
                << f.smoothness << ' ' << (f.lookAtTarget ? 1 : 0) << '\n';
        }

        if (registry.all_of<ECS::GameCameraComponent>(entity)) {
            const auto& g = registry.get<ECS::GameCameraComponent>(entity);
            out << "game_camera " << (g.primary ? 1 : 0) << '\n';
        }

        if (registry.all_of<ECS::RigidBodyComponent>(entity)) {
            const auto& rb = registry.get<ECS::RigidBodyComponent>(entity);
            out << "rigidbody "
                << static_cast<int>(rb.motionType) << ' '
                << rb.friction << ' ' << rb.restitution << ' '
                << rb.linearDamping << ' ' << rb.angularDamping << ' '
                << rb.gravityScale << ' '
                << (rb.continuous ? 1 : 0) << ' ' << (rb.allowSleep ? 1 : 0) << '\n';
        }

        if (registry.all_of<ECS::BoxColliderComponent>(entity)) {
            const auto& c = registry.get<ECS::BoxColliderComponent>(entity);
            out << "box_collider "
                << c.halfExtent.x << ' ' << c.halfExtent.y << ' ' << c.halfExtent.z << ' '
                << c.offset.x << ' ' << c.offset.y << ' ' << c.offset.z << ' '
                << (c.isTrigger ? 1 : 0) << '\n';
        }

        if (registry.all_of<ECS::SphereColliderComponent>(entity)) {
            const auto& c = registry.get<ECS::SphereColliderComponent>(entity);
            out << "sphere_collider "
                << c.radius << ' '
                << c.offset.x << ' ' << c.offset.y << ' ' << c.offset.z << ' '
                << (c.isTrigger ? 1 : 0) << '\n';
        }

        if (registry.all_of<ECS::CapsuleColliderComponent>(entity)) {
            const auto& c = registry.get<ECS::CapsuleColliderComponent>(entity);
            out << "capsule_collider "
                << c.radius << ' ' << c.halfHeight << ' '
                << c.offset.x << ' ' << c.offset.y << ' ' << c.offset.z << ' '
                << (c.isTrigger ? 1 : 0) << '\n';
        }

        if (registry.all_of<::Mesh>(entity)) {
            const auto& mesh = registry.get<::Mesh>(entity);
            std::string primitiveType;
            if (isPrimitiveMeshPath(mesh.meshPath, primitiveType)) {
                out << "primitive " << escapeString(primitiveType) << '\n';
            }
        }

        if (registry.all_of<ECS::MaterialComponent>(entity)) {
            const auto material = toSerializedMaterial(registry.get<ECS::MaterialComponent>(entity));
            out << "material "
                << material.baseColor.r << ' ' << material.baseColor.g << ' ' << material.baseColor.b << ' ' << material.baseColor.a << ' '
                << material.metallic << ' ' << material.roughness << ' ' << material.ambientOcclusion << ' '
                << material.emissiveFactor.x << ' ' << material.emissiveFactor.y << ' ' << material.emissiveFactor.z << ' '
                << material.alphaMode << ' ' << material.alphaCutoff << ' '
                << (material.doubleSided ? 1 : 0) << ' ' << (material.invertCulling ? 1 : 0) << '\n';
        }

        if (registry.all_of<ECS::ScriptComponent>(entity)) {
            const auto& scriptComponent = registry.get<ECS::ScriptComponent>(entity);
            for (const auto& script : scriptComponent.scripts) {
                std::vector<std::string> fieldNames;
                fieldNames.reserve(script.fields.size());
                for (const auto& [name, _] : script.fields) {
                    fieldNames.push_back(name);
                }
                std::sort(fieldNames.begin(), fieldNames.end());

                out << "script " << (script.enabled ? 1 : 0) << ' ' << escapeString(script.scriptPath) << ' ' << fieldNames.size() << '\n';
                for (const auto& fieldName : fieldNames) {
                    out << "field " << escapeString(fieldName) << ' ';
                    writeFieldValue(out, script.fields.at(fieldName));
                    out << '\n';
                }
                out << "endscript\n";
            }
        }

        out << "endentity\n";
    }

    return true;
}

bool SceneSerializer::loadFromFile(const std::string& path, SerializedScene& outScene) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::string token;
    if (!(in >> token) || token != kMagic) {
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
            in >> current->material.baseColor.r >> current->material.baseColor.g >> current->material.baseColor.b >> current->material.baseColor.a
               >> current->material.metallic >> current->material.roughness >> current->material.ambientOcclusion
               >> current->material.emissiveFactor.x >> current->material.emissiveFactor.y >> current->material.emissiveFactor.z
               >> current->material.alphaMode >> current->material.alphaCutoff >> doubleSided >> invertCulling;
            current->material.doubleSided = (doubleSided != 0);
            current->material.invertCulling = (invertCulling != 0);
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
                if (!readFieldValue(in, typeName, field.value)) return false;
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

} // namespace Atlas
