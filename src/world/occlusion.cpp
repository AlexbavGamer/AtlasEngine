#include "occlusion.h"

#include <algorithm>

namespace Atlas {

OcclusionCuller::OcclusionCuller(const OcclusionConfig& config)
    : m_Config(config)
    , m_Width(config.width > 0 ? config.width : 160)
    , m_Height(config.height > 0 ? config.height : 90) {
    m_Depth.assign(m_Width * m_Height, 1.0f);
}

void OcclusionCuller::setConfig(const OcclusionConfig& cfg) {
    m_Config = cfg;
    const uint32_t w = cfg.width > 0 ? cfg.width : 160;
    const uint32_t h = cfg.height > 0 ? cfg.height : 90;
    if (w != m_Width || h != m_Height) {
        m_Width = w;
        m_Height = h;
        m_Depth.assign(m_Width * m_Height, 1.0f);
    }
}

void OcclusionCuller::clear() {
    std::fill(m_Depth.begin(), m_Depth.end(), 1.0f);
    m_OccluderCount = 0;
    m_Stats = OcclusionStats{};
}

void OcclusionCuller::beginFrame(const glm::mat4& viewProj, uint32_t viewportWidth, uint32_t viewportHeight) {
    (void)viewportWidth;
    (void)viewportHeight;
    m_ViewProj = viewProj;
    clear();
}

bool OcclusionCuller::project(const glm::vec3& world, int& outX, int& outY, float& outDepth) const {
    glm::vec4 clip = m_ViewProj * glm::vec4(world, 1.0f);
    if (clip.w <= 1e-6f) {
        return false; // behind (or on) the camera
    }
    const float invW = 1.0f / clip.w;
    const float ndcX = clip.x * invW;
    const float ndcY = clip.y * invW;
    const float ndcZ = clip.z * invW; // Vulkan: [0..1], nearer = smaller
    outX = static_cast<int>((ndcX * 0.5f + 0.5f) * static_cast<float>(m_Width));
    outY = static_cast<int>((ndcY * 0.5f + 0.5f) * static_cast<float>(m_Height));
    outDepth = ndcZ;
    return true;
}

// Unit-cube corner signs for AABB corner expansion (avoids bit tricks).
static const glm::vec3 kBoxCorners[8] = {
    {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
    {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1},
};

void OcclusionCuller::addOccluder(const glm::vec3& boxMin, const glm::vec3& boxMax) {
    if (m_OccluderCount >= m_Config.maxOccluders) {
        return;
    }
    // 8 corners; skip boxes spanning the near plane (conservative).
    const glm::vec3 extent = boxMax - boxMin;
    glm::vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        corners[i] = boxMin + kBoxCorners[i] * extent;
    }
    int minX = static_cast<int>(m_Width), minY = static_cast<int>(m_Height);
    int maxX = -1, maxY = -1;
    float nearest = 1.0f;
    for (const auto& c : corners) {
        int px = 0, py = 0;
        float d = 1.0f;
        if (!project(c, px, py, d)) {
            return; // spans near plane: contribute nothing
        }
        minX = std::min(minX, px);
        minY = std::min(minY, py);
        maxX = std::max(maxX, px);
        maxY = std::max(maxY, py);
        nearest = std::min(nearest, d);
    }
    minX = std::clamp(minX, 0, static_cast<int>(m_Width) - 1);
    minY = std::clamp(minY, 0, static_cast<int>(m_Height) - 1);
    maxX = std::clamp(maxX, 0, static_cast<int>(m_Width) - 1);
    maxY = std::clamp(maxY, 0, static_cast<int>(m_Height) - 1);
    if (maxX < minX || maxY < minY) {
        return; // fully off-screen
    }
    for (int y = minY; y <= maxY; ++y) {
        float* row = m_Depth.data() + static_cast<size_t>(y) * m_Width;
        for (int x = minX; x <= maxX; ++x) {
            row[x] = std::min(row[x], nearest);
        }
    }
    m_OccluderCount++;
    m_Stats.occludersUsed = m_OccluderCount;
}

bool OcclusionCuller::isOccludedBox(const glm::vec3& boxMin, const glm::vec3& boxMax) const {
    m_Stats.boxTests++;
    if (m_OccluderCount == 0) {
        return false;
    }
    const glm::vec3 extent = boxMax - boxMin;
    for (int i = 0; i < 8; ++i) {
        glm::vec3 c = boxMin + kBoxCorners[i] * extent;
        int px = 0, py = 0;
        float d = 1.0f;
        if (!project(c, px, py, d)) {
            return false; // corner behind camera: conservatively visible
        }
        if (px < 0 || py < 0 || px >= static_cast<int>(m_Width) || py >= static_cast<int>(m_Height)) {
            return false; // corner off-screen: conservatively visible
        }
        const float stored = m_Depth[static_cast<size_t>(py) * m_Width + static_cast<size_t>(px)];
        if (stored + m_Config.depthBias >= d) {
            return false; // this corner is in front of stored depth: visible
        }
    }
    m_Stats.boxesCulled++;
    return true;
}

bool OcclusionCuller::isOccluded(const glm::vec3& center, float radius) const {
    const glm::vec3 r(radius);
    return isOccludedBox(center - r, center + r);
}

} // namespace Atlas
