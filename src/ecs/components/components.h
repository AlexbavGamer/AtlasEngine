#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../../core/string/string_id.h"

namespace Atlas { namespace ECS {

struct MeshComponent {
    std::string meshPath;
    
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;

    bool isValid() const {
        return vertexBuffer != VK_NULL_HANDLE && indexBuffer != VK_NULL_HANDLE;
    }
};

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
    enum class Type : uint32_t {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    Type type = Type::Point;
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;

    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;

    float cutOff = glm::cos(glm::radians(12.5f));
    float outerCutOff = glm::cos(glm::radians(15.0f));

    bool castShadows = false;
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

}}
