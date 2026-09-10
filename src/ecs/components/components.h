#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../../core/string/string_id.h"
#include "../../animation/animation.h"
#include "../../scripting/script_types.h"
#include "../reflection.h"

namespace Atlas { namespace ECS {

struct MaterialComponent {
    glm::vec4 baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ambientOcclusion = 1.0f;

    glm::vec3 emissiveFactor = glm::vec3(0.0f);

    enum class AlphaMode : uint8_t {
        Opaque = 0,
        Mask = 1,
        Blend = 2,
    };

    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool invertCulling = false;

    StringID albedoTextureId;
    std::string albedoTexturePath;

    StringID normalTextureId;
    std::string normalTexturePath;

    StringID metallicRoughnessTextureId;
    std::string metallicRoughnessTexturePath;

    StringID aoTextureId;
    std::string aoTexturePath;

    StringID emissiveTextureId;
    std::string emissiveTexturePath;

    int32_t albedoTextureIndex = -1;
    int32_t normalTextureIndex = -1;
    int32_t metallicRoughnessTextureIndex = -1;
    int32_t aoTextureIndex = -1;
    int32_t emissiveTextureIndex = -1;

    bool useAlbedoTexture = false;
    bool useNormalTexture = false;
    bool useMetallicRoughnessTexture = false;
    bool useAOTexture = false;
    bool useEmissiveTexture = false;
};



struct RenderableComponent {
    bool visible = true;
    uint32_t renderOrder = 0;
    uint32_t meshID = 0;
    uint32_t materialID = 0;
};

// HLOD Level of Detail
enum class HLODLevel : uint8_t {
    FullDetail = 0,
    HLOD0 = 1,
    HLOD1 = 2,
    Count
};

// Component to track HLOD level for an entity
struct HLODComponent {
    HLODLevel currentLevel = HLODLevel::FullDetail;
    HLODLevel targetLevel = HLODLevel::FullDetail;
    float screenSizeBias = 1.0f;  // Screen size threshold for LOD transition
    entt::entity ownerEntity = entt::null;
};



// Component to store HLOD mesh reference (for rendering)
struct HLODMeshRef {
    uint32_t meshID = 0;       // ID referencing loaded HLOD mesh data
    uint32_t materialID = 0;   // ID referencing material
    uint32_t instanceCount = 0; // Number of instances to render
};

// HLODActor / HLODMesh are defined in src/world/hlod.h (world system).
// Only the per-entity components live here to avoid ODR duplication.

struct CameraComponent {
    glm::vec3 position = glm::vec3(0.0f, 2.0f, 5.0f);
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    
    bool isActive = true;
    
    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, target, up);
    }
    
    glm::mat4 getProjectionMatrix(float aspectRatio) const {
        return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    }
};

struct TagComponent {
    std::string name;
    
    TagComponent() = default;
    TagComponent(const std::string& n) : name(n) {}
};

struct LightComponent {
    // Type encoded as uint32_t for shader compatibility (packed into direction.w)
    // 0 = Directional, 1 = Point, 2 = Spot
    enum class Type : uint32_t { Directional = 0, Point = 1, Spot = 2 };
    Type type = Type::Point;
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;

    // For directional lights: direction vector (normalized)
    // For point/spot: unused (position comes from Transform)
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);

    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;

    float cutOff = glm::cos(glm::radians(12.5f));
    float outerCutOff = glm::cos(glm::radians(15.0f));

    bool castShadows = false;
};

// Procedural sun: a single directional light driven by azimuth/elevation
// (no Transform needed). The renderer uploads the first SunComponent found
// as the slot-0 directional light + shadow caster, and the sky uses the
// same direction for its sun disk. Vulkan-free (glm only).
struct SunComponent {
    float azimuthDeg = 135.0f;   // compass around Y: 0 = +Z, 90 = +X (east)
    float elevationDeg = 50.0f;  // above horizon, clamped to [-12, 90]
    glm::vec3 color = glm::vec3(1.0f, 0.96f, 0.90f);
    float intensity = 3.0f;
    bool castShadows = true;
    // Shadow frustum half-extent in meters (smaller = sharper shadows, less
    // coverage). The map is fixed 2048px, so texel size = 2*range/2048.
    float shadowRange = 80.0f;

    // Unit vector pointing FROM the scene TOWARD the sun.
    glm::vec3 sunDirection() const {
        const float el = glm::radians(std::clamp(elevationDeg, -90.0f, 90.0f));
        const float az = glm::radians(azimuthDeg);
        const float ce = std::cos(el);
        return glm::vec3(ce * std::sin(az), std::sin(el), ce * std::cos(az));
    }
    // Travel direction of the light rays (what LightBuffer/shader expect:
    // frag uses L = normalize(-light.direction)).
    glm::vec3 lightDirection() const { return -sunDirection(); }

    // Convenience: hour in [0, 24) drives az/el along a full daily cycle
    // (6h = sunrise east, 12h = peak, 18h = sunset west, 0h/24h = nadir,
    // 65 deg below the horizon). Night hours yield negative elevation —
    // the renderer fades the light out and darkens the sky.
    void setTimeOfDay(float hour) {
        const float h = std::clamp(hour, 0.0f, 24.0f);
        const float dayAngle = (h - 6.0f) / 12.0f * 3.14159265f;
        elevationDeg = std::sin(dayAngle) * 65.0f;
        azimuthDeg = 90.0f + (h - 6.0f) * 15.0f;
    }
};

// Procedural sky backdrop (gradient + sun disk + horizon glow), drawn as a
// fullscreen triangle at the far plane before opaques. First enabled
// SkyComponent in the scene wins; none = legacy clear-color background.
struct SkyComponent {
    bool enabled = true;
    glm::vec3 horizonColor = glm::vec3(0.62f, 0.72f, 0.83f);
    glm::vec3 zenithColor = glm::vec3(0.19f, 0.36f, 0.63f);
    glm::vec3 groundColor = glm::vec3(0.09f, 0.09f, 0.11f);
    glm::vec3 sunColor = glm::vec3(1.0f, 0.88f, 0.70f);
    float sunDiskSizeDeg = 2.5f;  // angular radius of the disk
    float sunGlow = 0.35f;        // halo strength around the disk
};



struct ParentComponent {
    entt::entity parent = entt::null;
};



struct ChildrenComponent {
    std::vector<entt::entity> children;
};



// Editor-only state used for soft deletes/hiding entities without releasing GPU resources.
struct EditorHiddenComponent {
    bool hidden = true;
};



struct ScriptEntry {
    bool enabled = true;
    std::string scriptPath;
    Atlas::Scripting::ScriptFieldMap fields;
};

struct ScriptComponent {
    std::vector<ScriptEntry> scripts;
};

struct FollowCameraComponent {
    entt::entity target = entt::null;
    glm::vec3 offset = glm::vec3(0.0f, 2.0f, 5.0f);
    float smoothness = 8.0f;
    bool lookAtTarget = true;
};

struct GameCameraComponent {
    bool primary = true;
};

enum class PhysicsMotionType : uint8_t {
    Static = 0,
    Dynamic = 1,
    Kinematic = 2,
};

struct RigidBodyComponent {
    PhysicsMotionType motionType = PhysicsMotionType::Static;
    float friction = 0.5f;
    float restitution = 0.0f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    float gravityScale = 1.0f;
    bool continuous = false;
    bool allowSleep = true;
};



struct BoxColliderComponent {
    glm::vec3 halfExtent = glm::vec3(0.5f);
    glm::vec3 offset = glm::vec3(0.0f);
    bool isTrigger = false;
};



struct SphereColliderComponent {
    float radius = 0.5f;
    glm::vec3 offset = glm::vec3(0.0f);
    bool isTrigger = false;
};



struct CapsuleColliderComponent {
    float radius = 0.5f;
    float halfHeight = 0.5f;
    glm::vec3 offset = glm::vec3(0.0f);
    bool isTrigger = false;
};



// Triangle-mesh collider (V1). Source geometry comes from the entity's own
// ::Mesh component (meshPath): file meshes are reloaded from disk and cached
// per path, primitives are generated procedurally. Static/kinematic bodies
// get exact triangle collision; dynamic bodies fall back to their OBB.
struct MeshColliderComponent {
    glm::vec3 offset = glm::vec3(0.0f);
    bool isTrigger = false;
    // Reserved: convex-hull approximation path (currently triangle soup).
    bool convex = false;
};



// Skeletal animation (V1): one skeleton shared across skinned meshes + one clip player per entity.
struct SkeletonComponent {
    std::shared_ptr<Atlas::Anim::Skeleton> skeleton;
    std::vector<Atlas::Anim::AnimationClip> clips;
};



struct AnimationPlayerComponent {
    Atlas::Anim::AnimationPlayer player;
};



struct BonePoseOverrideComponent {
    bool enabled = false;
    std::vector<uint8_t> hasRotation;
    std::vector<glm::quat> rotation;
};



struct SkinnedMeshComponent {
    // Entity that owns the skeleton/clips/player state (usually the model root).
    entt::entity skeletonEntity = entt::null;

    // Dynamic offset (bytes) into the renderer bone palette buffer for the current frame.
    uint32_t bonePaletteOffsetBytes = 0;
};



}} // namespace Atlas::ECS

// ============================================================================
// Component field registrations (global scope: explicit specializations of
// ecs::refl::ComponentFields must live in ecs::refl, so they cannot be nested
// inside namespace Atlas::ECS).
// ============================================================================

COMPONENT_FIELDS(Atlas::ECS::MaterialComponent,
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, baseColor, "Base Color")
        .tooltip("Base color of the material")
        .color_picker(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, metallic, "Metallic")
        .range(0.0f, 1.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, roughness, "Roughness")
        .range(0.0f, 1.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, ambientOcclusion, "Ambient Occlusion")
        .range(0.0f, 1.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, emissiveFactor, "Emissive Factor")
        .tooltip("Emissive color factor")
        .color_picker(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, alphaMode, "Alpha Mode")
        .tooltip("Alpha blending mode"),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, alphaCutoff, "Alpha Cutoff")
        .range(0.0f, 1.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, doubleSided, "Double Sided"),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, invertCulling, "Invert Culling"),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, albedoTextureId, "Albedo Texture ID")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, albedoTexturePath, "Albedo Texture Path")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, useAlbedoTexture, "Use Albedo Texture"),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, normalTextureId, "Normal Texture ID")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, normalTexturePath, "Normal Texture Path")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, useNormalTexture, "Use Normal Texture"),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, metallicRoughnessTextureId, "Metallic/Roughness Texture ID")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, metallicRoughnessTexturePath, "Metallic/Roughness Texture Path")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, useMetallicRoughnessTexture, "Use Metallic/Roughness Texture"),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, aoTextureId, "AO Texture ID")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, aoTexturePath, "AO Texture Path")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, useAOTexture, "Use AO Texture"),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, emissiveTextureId, "Emissive Texture ID")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, emissiveTexturePath, "Emissive Texture Path")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::MaterialComponent, useEmissiveTexture, "Use Emissive Texture"))

COMPONENT_FIELDS(Atlas::ECS::HLODComponent,
    COMPONENT_FIELD(Atlas::ECS::HLODComponent, currentLevel, "Current Level")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::HLODComponent, targetLevel, "Target Level")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::HLODComponent, screenSizeBias, "Screen Size Bias")
        .range(0.1f, 8.0f)
        .step(0.1f),
    COMPONENT_FIELD(Atlas::ECS::HLODComponent, ownerEntity, "Owner Entity")
        .read_only(true))

COMPONENT_FIELDS(Atlas::ECS::LightComponent,
    COMPONENT_FIELD(Atlas::ECS::LightComponent, type, "Type")
        .tooltip("Light type: 0=Directional, 1=Point, 2=Spot")
        .range(0, 2)
        .step(1),
    COMPONENT_FIELD(Atlas::ECS::LightComponent, color, "Color")
        .color_picker(true),
    COMPONENT_FIELD(Atlas::ECS::LightComponent, intensity, "Intensity")
        .range(0.0f, 100.0f)
        .step(0.1f),
    COMPONENT_FIELD(Atlas::ECS::LightComponent, direction, "Direction")
        .tooltip("Light direction (for directional lights)"),
    COMPONENT_FIELD(Atlas::ECS::LightComponent, constant, "Constant Attenuation")
        .range(0.0f, 10.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::LightComponent, linear, "Linear Attenuation")
        .range(0.0f, 1.0f)
        .step(0.001f),
    COMPONENT_FIELD(Atlas::ECS::LightComponent, quadratic, "Quadratic Attenuation")
        .range(0.0f, 1.0f)
        .step(0.001f),
    COMPONENT_FIELD(Atlas::ECS::LightComponent, cutOff, "Cutoff Angle")
        .range(0.0f, 90.0f)
        .step(0.1f),
    COMPONENT_FIELD(Atlas::ECS::LightComponent, outerCutOff, "Outer Cutoff")
        .range(0.0f, 90.0f)
        .step(0.1f),
    COMPONENT_FIELD(Atlas::ECS::LightComponent, castShadows, "Cast Shadows")
        .tooltip("Enable shadow casting for this light"))

COMPONENT_FIELDS(Atlas::ECS::ParentComponent,
    COMPONENT_FIELD(Atlas::ECS::ParentComponent, parent, "Parent Entity")
        .read_only(true))

COMPONENT_FIELDS(Atlas::ECS::ChildrenComponent,
    COMPONENT_FIELD(Atlas::ECS::ChildrenComponent, children, "Children")
        .read_only(true))

COMPONENT_FIELDS(Atlas::ECS::EditorHiddenComponent,
    COMPONENT_FIELD(Atlas::ECS::EditorHiddenComponent, hidden, "Hidden"))

COMPONENT_FIELDS(Atlas::ECS::RigidBodyComponent,
    COMPONENT_FIELD(Atlas::ECS::RigidBodyComponent, motionType, "Motion Type")
        .tooltip("Static / Dynamic / Kinematic"),
    COMPONENT_FIELD(Atlas::ECS::RigidBodyComponent, friction, "Friction")
        .range(0.0f, 2.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::RigidBodyComponent, restitution, "Restitution")
        .range(0.0f, 2.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::RigidBodyComponent, linearDamping, "Linear Damping")
        .range(0.0f, 10.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::RigidBodyComponent, angularDamping, "Angular Damping")
        .range(0.0f, 10.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::RigidBodyComponent, gravityScale, "Gravity Scale")
        .range(-10.0f, 10.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::RigidBodyComponent, continuous, "Continuous Collision"),
    COMPONENT_FIELD(Atlas::ECS::RigidBodyComponent, allowSleep, "Allow Sleep"))

COMPONENT_FIELDS(Atlas::ECS::BoxColliderComponent,
    COMPONENT_FIELD(Atlas::ECS::BoxColliderComponent, halfExtent, "Half Extent")
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::BoxColliderComponent, offset, "Offset")
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::BoxColliderComponent, isTrigger, "Is Trigger"))

COMPONENT_FIELDS(Atlas::ECS::SphereColliderComponent,
    COMPONENT_FIELD(Atlas::ECS::SphereColliderComponent, radius, "Radius")
        .range(0.01f, 100.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::SphereColliderComponent, offset, "Offset")
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::SphereColliderComponent, isTrigger, "Is Trigger"))

COMPONENT_FIELDS(Atlas::ECS::CapsuleColliderComponent,
    COMPONENT_FIELD(Atlas::ECS::CapsuleColliderComponent, radius, "Radius")
        .range(0.01f, 100.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::CapsuleColliderComponent, halfHeight, "Half Height")
        .range(0.01f, 100.0f)
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::CapsuleColliderComponent, offset, "Offset")
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::CapsuleColliderComponent, isTrigger, "Is Trigger"))

COMPONENT_FIELDS(Atlas::ECS::MeshColliderComponent,
    COMPONENT_FIELD(Atlas::ECS::MeshColliderComponent, offset, "Offset")
        .step(0.01f),
    COMPONENT_FIELD(Atlas::ECS::MeshColliderComponent, isTrigger, "Is Trigger"),
    COMPONENT_FIELD(Atlas::ECS::MeshColliderComponent, convex, "Convex (reserved)")
        .tooltip("Reserved for future convex-hull approximation")
        .read_only(true))

COMPONENT_FIELDS(Atlas::ECS::SkeletonComponent,
    COMPONENT_FIELD(Atlas::ECS::SkeletonComponent, skeleton, "Skeleton")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::SkeletonComponent, clips, "Clips")
        .read_only(true))

COMPONENT_FIELDS(Atlas::ECS::AnimationPlayerComponent,
    COMPONENT_FIELD(Atlas::ECS::AnimationPlayerComponent, player, "Animation Player")
        .read_only(true))

COMPONENT_FIELDS(Atlas::ECS::BonePoseOverrideComponent,
    COMPONENT_FIELD(Atlas::ECS::BonePoseOverrideComponent, enabled, "Enabled"),
    COMPONENT_FIELD(Atlas::ECS::BonePoseOverrideComponent, hasRotation, "Has Rotation")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::BonePoseOverrideComponent, rotation, "Rotation")
        .read_only(true))

COMPONENT_FIELDS(Atlas::ECS::SkinnedMeshComponent,
    COMPONENT_FIELD(Atlas::ECS::SkinnedMeshComponent, skeletonEntity, "Skeleton Entity")
        .tooltip("Entity owning the skeleton (usually the model root)")
        .read_only(true),
    COMPONENT_FIELD(Atlas::ECS::SkinnedMeshComponent, bonePaletteOffsetBytes, "Bone Palette Offset")
        .tooltip("Per-frame byte offset into the renderer bone palette")
        .read_only(true))


// ============================================================================
// Custom field renderers for engine types used by the components above.
// Must live in ecs::refl::detail (NOT nested inside Atlas::ECS) and be
// declared before any renderAutoComponentProperties<T> instantiation
// (i.e. before ui_manager.cpp uses them).
// ============================================================================

namespace ecs { namespace refl { namespace detail {

template <>
struct FieldRenderer<Atlas::StringID> {
    static void renderFieldImpl(Atlas::StringID& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "StringID";
        ImGui::Text("%s: #%llu", label,
                    static_cast<unsigned long long>(v.getID()));
    }
};

template <>
struct FieldRenderer<entt::entity> {
    static void renderFieldImpl(entt::entity& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "Entity";
        if (v == entt::null) {
            ImGui::Text("%s: (none)", label);
        } else {
            ImGui::Text("%s: %u", label, static_cast<uint32_t>(v));
        }
    }
};

template <>
struct FieldRenderer<Atlas::ECS::PhysicsMotionType> {
    static void renderFieldImpl(Atlas::ECS::PhysicsMotionType& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "Motion Type";
        const char* items[] = {"Static", "Dynamic", "Kinematic"};
        int current = static_cast<int>(v);
        if (current < 0 || current > 2) current = 0;
        if (ImGui::Combo(label, &current, items, 3)) {
            v = static_cast<Atlas::ECS::PhysicsMotionType>(current);
        }
    }
};

template <>
struct FieldRenderer<Atlas::ECS::MaterialComponent::AlphaMode> {
    static void renderFieldImpl(Atlas::ECS::MaterialComponent::AlphaMode& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "Alpha Mode";
        const char* items[] = {"Opaque", "Mask", "Blend"};
        int current = static_cast<int>(v);
        if (current < 0 || current > 2) current = 0;
        if (ImGui::Combo(label, &current, items, 3)) {
            v = static_cast<Atlas::ECS::MaterialComponent::AlphaMode>(current);
        }
    }
};

template <>
struct FieldRenderer<Atlas::ECS::HLODLevel> {
    static void renderFieldImpl(Atlas::ECS::HLODLevel& v, const FieldMeta& m) {
        const char* label = (m.label && m.label[0]) ? m.label : "HLOD Level";
        if (m.read_only) {
            const char* names[] = {"Full Detail", "HLOD0", "HLOD1"};
            const int idx = static_cast<int>(v);
            ImGui::Text("%s: %s", label, (idx >= 0 && idx < 3) ? names[idx] : "(unknown)");
            return;
        }
        const char* items[] = {"Full Detail", "HLOD0", "HLOD1"};
        int current = static_cast<int>(v);
        if (current < 0 || current > 2) current = 0;
        if (ImGui::Combo(label, &current, items, 3)) {
            v = static_cast<Atlas::ECS::HLODLevel>(current);
        }
    }
};

} } } // namespace ecs::refl::detail
  
