#pragma once

// TDD §7 — software occlusion culling (CPU depth rasterizer).
//
// Conservative hierarchical-Z style culling without GPU readback:
// per frame, big occluders (buildings) are rasterized as boxes into a
// low-res depth buffer; cells/objects whose bounding box is fully behind
// stored depth are culled. Boxes spanning the near plane are skipped
// (conservative: no occlusion contributed). Temporal coherence: occluders
// come from the previous frame's LOD screen-size data.

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace Atlas {

struct OcclusionConfig {
    bool enable = false; // opt-in via UI; off by default (conservative)
    uint32_t width = 160;
    uint32_t height = 90;
    uint32_t maxOccluders = 64;
    float depthBias = 0.004f;
    // Only entities with screenSize (fraction of viewport height) above this
    // become occluders.
    float minOccluderScreenSize = 0.12f;
};

struct OcclusionStats {
    uint32_t occludersUsed = 0;
    uint32_t boxTests = 0;
    uint32_t boxesCulled = 0;
};

class OcclusionCuller {
public:
    explicit OcclusionCuller(const OcclusionConfig& config = OcclusionConfig{});

    void setConfig(const OcclusionConfig& cfg);
    const OcclusionConfig& getConfig() const { return m_Config; }
    const OcclusionStats& getStats() const { return m_Stats; }

    // Start a frame. viewProj = projection * view (Vulkan convention).
    void beginFrame(const glm::mat4& viewProj, uint32_t viewportWidth, uint32_t viewportHeight);

    // Rasterize a world-space AABB as an occluder (nearest depth wins).
    // Silently ignored when the occluder budget is exhausted.
    void addOccluder(const glm::vec3& boxMin, const glm::vec3& boxMax);

    // Conservative test: true only if ALL 8 box corners are behind stored
    // depth (with bias). Anything spanning the near plane returns false.
    bool isOccludedBox(const glm::vec3& boxMin, const glm::vec3& boxMax) const;

    // Sphere wrapper (builds the sphere AABB).
    bool isOccluded(const glm::vec3& center, float radius) const;

    void clear();

private:
    // Project world point to pixel + [0..1] depth. Returns false if behind camera.
    bool project(const glm::vec3& world, int& outX, int& outY, float& outDepth) const;

    OcclusionConfig m_Config{};
    mutable OcclusionStats m_Stats{}; // mutated by const query methods
    glm::mat4 m_ViewProj{1.0f};
    uint32_t m_Width = 160;
    uint32_t m_Height = 90;
    std::vector<float> m_Depth; // row-major, init 1.0 (far)
    uint32_t m_OccluderCount = 0;
};

} // namespace Atlas
