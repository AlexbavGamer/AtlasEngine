#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan.h>

#include "../ecs/vertex.h"
#include "../utils/model_loader.h"

namespace Atlas::PrimitiveHelpers {

// Find a Vulkan memory type — callback compatible with ModelLoader::createBuffers.
inline uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties,
                               VkPhysicalDeviceMemoryProperties* memProperties) {
    for (uint32_t i = 0; i < memProperties->memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProperties->memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return uint32_t(~0u);
}

// ── Primitive mesh generators ──────────────────────────────────────────────

inline MeshData createPlane(float size = 1.0f) {
    MeshData mesh;
    const float h = size * 0.5f;
    mesh.name = "Plane";
    mesh.vertices = {
        Vertex{{-h, 0.0f, -h}, {0.85f, 0.85f, 0.85f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        Vertex{{ h, 0.0f, -h}, {0.85f, 0.85f, 0.85f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        Vertex{{ h, 0.0f,  h}, {0.85f, 0.85f, 0.85f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        Vertex{{-h, 0.0f,  h}, {0.85f, 0.85f, 0.85f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2, 2, 3, 0};
    mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
}

inline MeshData createSphere(float radius = 0.5f, int segments = 24, int rings = 16) {
    MeshData mesh;
    mesh.name = "Sphere";

    for (int y = 0; y <= rings; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(rings);
        const float phi = v * glm::pi<float>();
        for (int x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float theta = u * glm::two_pi<float>();

            glm::vec3 normal(
                std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta));
            glm::vec3 pos = normal * radius;
            glm::vec3 color(0.92f, 0.92f, 0.92f);
            mesh.vertices.push_back(Vertex{pos, color, glm::vec2(u, v), glm::normalize(normal)});
        }
    }

    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(y * (segments + 1) + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(segments + 1);
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
}

inline MeshData createCylinder(float radius = 0.5f, float height = 1.0f, int segments = 24) {
    MeshData mesh;
    mesh.name = "Cylinder";
    const float halfH = height * 0.5f;

    // Vertices: side
    for (int i = 0; i <= segments; ++i) {
        const float theta = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        const float x = std::cos(theta) * radius;
        const float z = std::sin(theta) * radius;
        const glm::vec3 n = glm::normalize(glm::vec3(x, 0.0f, z));
        mesh.vertices.emplace_back(Vertex{glm::vec3{x, -halfH, z}, glm::vec3{0.85f}, glm::vec2{0.0f}, n});
        mesh.vertices.emplace_back(Vertex{glm::vec3{x,  halfH, z}, glm::vec3{0.85f}, glm::vec2{0.0f}, n});
    }
    // Top cap center
    const uint32_t centerTop = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.emplace_back(Vertex{glm::vec3{0.0f,  halfH, 0.0f}, glm::vec3{0.85f}, glm::vec2{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}});
    // Bottom cap center
    const uint32_t centerBot = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.emplace_back(Vertex{glm::vec3{0.0f, -halfH, 0.0f}, glm::vec3{0.85f}, glm::vec2{0.0f}, glm::vec3{0.0f, -1.0f, 0.0f}});

    // Side indices (two per segment)
    for (int i = 0; i < segments; ++i) {
        const uint32_t a = static_cast<uint32_t>(i * 2);
        const uint32_t b = a + 1;
        const uint32_t c = static_cast<uint32_t>((i + 1) * 2);
        const uint32_t d = c + 1;
        mesh.indices.insert(mesh.indices.end(), {a, b, c, c, b, d});
    }
    // Top cap
    for (int i = 0; i < segments; ++i) {
        const uint32_t a = static_cast<uint32_t>(i * 2 + 1);
        const uint32_t b = static_cast<uint32_t>(((i + 1) % segments) * 2 + 1);
        mesh.indices.insert(mesh.indices.end(), {centerTop, b, a});
    }
    // Bottom cap
    for (int i = 0; i < segments; ++i) {
        const uint32_t a = static_cast<uint32_t>(i * 2);
        const uint32_t b = static_cast<uint32_t>(((i + 1) % segments) * 2);
        mesh.indices.insert(mesh.indices.end(), {centerBot, a, b});
    }

    mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
}

inline MeshData createCapsule(float radius = 0.5f, float height = 2.0f, int segments = 24, int hemiRings = 8) {
    MeshData mesh;
    mesh.name = "Capsule";
    const float halfH = height * 0.5f;

    // Hemisphere top
    for (int y = 0; y <= hemiRings; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(hemiRings);
        const float phi = v * glm::pi<float>() * 0.5f;
        for (int x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            mesh.vertices.emplace_back(Vertex{n * radius + glm::vec3(0.0f, halfH, 0.0f), glm::vec3{0.85f}, glm::vec2{u, v}, n});
        }
    }
    // Cylinder middle
    for (int y = 0; y <= 1; ++y) {
        const float yy = -halfH + static_cast<float>(y) * height;
        const float v = 0.5f + static_cast<float>(y) * 0.5f;
        for (int x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 n(std::cos(theta), 0.0f, std::sin(theta));
            mesh.vertices.emplace_back(Vertex{glm::vec3(n.x * radius, yy, n.z * radius), glm::vec3{0.85f}, glm::vec2{u, v}, n});
        }
    }
    // Hemisphere bottom
    for (int y = 0; y <= hemiRings; ++y) {
        const float v = 1.0f + static_cast<float>(y) / static_cast<float>(hemiRings);
        const float phi = glm::pi<float>() * 0.5f + static_cast<float>(y) / static_cast<float>(hemiRings) * glm::pi<float>() * 0.5f;
        for (int x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float theta = u * glm::two_pi<float>();
            const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            mesh.vertices.emplace_back(Vertex{n * radius + glm::vec3(0.0f, -halfH, 0.0f), glm::vec3{0.85f}, glm::vec2{u, v}, n});
        }
    }

    const int stride = segments + 1;
    const int topHemisphereEnd = (hemiRings + 1) * stride;
    const int middleEnd = topHemisphereEnd + 2 * stride;
    // Indices — top hemisphere
    for (int y = 0; y < hemiRings; ++y) {
        for (int x = 0; x < segments; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(y * stride + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(stride);
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    // Cylinder indices
    for (int y = 0; y < 1; ++y) {
        const int base = topHemisphereEnd + y * stride;
        for (int x = 0; x < segments; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(base + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = static_cast<uint32_t>(base + stride + x);
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    // Bottom hemisphere indices
    for (int y = 0; y < hemiRings; ++y) {
        const int base = middleEnd + y * stride;
        for (int x = 0; x < segments; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(base + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = static_cast<uint32_t>(base + stride + x);
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }

    mesh.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    return mesh;
}

} // namespace Atlas::PrimitiveHelpers