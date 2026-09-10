#include "world_partition.h"

#include "../scene/scene.h"
#include "../utils/frustum.h"
// ThreadPool available for future async real loading (placeholder).
// #include "../core/threading/thread_pool.h"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <limits>
#include <fstream>

namespace Atlas {

WorldPartition::WorldPartition(Scene* scene) : m_Scene(scene) {}

void WorldPartition::setScene(Scene* scene) {
    if (m_Scene == scene) return;
    m_Scene = scene;
    markDirty();
}

void WorldPartition::markDirty() {
    m_Dirty = true;
}

WorldCellCoord WorldPartition::worldToCell(const glm::vec3& p) const {
    const float cs = (m_Config.cellSize > 1e-3f) ? m_Config.cellSize : 1.0f;
    WorldCellCoord c;
    c.x = static_cast<int>(std::floor(p.x / cs));
    c.z = static_cast<int>(std::floor(p.z / cs));
    return c;
}

glm::vec3 WorldPartition::getCellCenter(const WorldCellCoord& c) const {
    glm::vec3 bmin, bmax;
    getCellBounds(c, bmin, bmax);
    glm::vec3 center = (bmin + bmax) * 0.5f;
    center.y = 0.0f; // streaming priority is horizontal; keep Y neutral
    return center;
}

void WorldPartition::getCellBounds(const WorldCellCoord& c, glm::vec3& outMin, glm::vec3& outMax) const {
    const float cs = (m_Config.cellSize > 1e-3f) ? m_Config.cellSize : 1.0f;
    outMin = glm::vec3(static_cast<float>(c.x) * cs, -10000.0f, static_cast<float>(c.z) * cs);
    outMax = glm::vec3(static_cast<float>(c.x + 1) * cs, 10000.0f, static_cast<float>(c.z + 1) * cs);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
float WorldPartition::effectiveDistance(float rawDist, float weight, float radius) const {
    if (radius > 0.0f && rawDist <= radius) {
        return 0.0f;
    }
    float w = (weight > 1e-4f) ? weight : 1.0f;
    float d = rawDist - std::max(0.0f, radius);
    if (d < 0.0f) d = 0.0f;
    return d / w;
}

float WorldPartition::distanceToNearestSource(const glm::vec3& cellCenter, float& outWeight, float& outRadius) const {
    // If no position sources and no legacy entity sources, fallback to camera.
    if (m_Sources.empty() && m_StreamingSources.empty()) {
        outWeight = 1.0f;
        outRadius = 0.0f;
        glm::vec3 cam = getCameraPosition();
        return glm::length(cellCenter - cam);
    }

    float bestRaw = std::numeric_limits<float>::max();
    float bestEff = std::numeric_limits<float>::max();
    float bestW = 1.0f;
    float bestR = 0.0f;

    for (const auto& s : m_Sources) {
        float raw = glm::length(cellCenter - s.position);
        float eff = effectiveDistance(raw, s.priorityWeight, s.radius);
        if (eff < bestEff) {
            bestEff = eff;
            bestRaw = raw;
            bestW = s.priorityWeight;
            bestR = s.radius;
        }
    }
    for (const auto& es : m_StreamingSources) {
        float raw = glm::length(cellCenter - es.lastPosition);
        float eff = raw; // weight 1
        if (eff < bestEff) {
            bestEff = eff;
            bestRaw = raw;
            bestW = 1.0f;
            bestR = 0.0f;
        }
    }
    // If still no best (should not happen), fallback to camera
    if (bestEff == std::numeric_limits<float>::max()) {
        outWeight = 1.0f;
        outRadius = 0.0f;
        glm::vec3 cam = getCameraPosition();
        return glm::length(cellCenter - cam);
    }
    outWeight = bestW;
    outRadius = bestR;
    return bestRaw;
}

float WorldPartition::computePriority(const glm::vec3& cellCenter, bool isPreload) const {
    float w, r;
    float raw = distanceToNearestSource(cellCenter, w, r);
    float eff = effectiveDistance(raw, w, r);
    float farDist = (m_Config.hlod1RangeMeters > 0.0f) ? m_Config.hlod1RangeMeters * 2.0f : 10000.0f;
    if (farDist < 1.0f) farDist = 10000.0f;
    float distFactor = 1.0f - std::min(eff / farDist, 1.0f);
    float pri = distFactor * 0.9f + 0.1f;
    if (isPreload) pri *= 0.4f;
    pri = std::clamp(pri, 0.0f, 1.0f);
    return pri;
}

// ---------------------------------------------------------------------------
// rebuild
// ---------------------------------------------------------------------------
namespace {
// TDD §8: rough resident-size estimate for one mesh entity.
// Matches the Vertex layout (pos12+color12+uv8+normal12+joints16+weights16=76B).
uint64_t estimateMeshEntityBytes(const ::Mesh& mesh) {
    return static_cast<uint64_t>(mesh.vertexCount) * 76u + static_cast<uint64_t>(mesh.indexCount) * 4u;
}
} // namespace

void WorldPartition::rebuild() {
    m_Cells.clear();
    m_LoadedCells.clear();
    m_LoadedMemoryBytes = 0;

    if (!m_Scene) {
        m_Dirty = false;
        return;
    }

    auto& registry = m_Scene->getRegistry();

    auto view = registry.view<::Mesh>();
    for (auto e : view) {
        if (!registry.valid(e)) continue;
        if (!registry.all_of<Transform>(e)) continue;

        glm::mat4 world = m_Scene->getCachedWorldTransform(e);
        glm::vec3 pos = glm::vec3(world[3]);
        WorldCellCoord cell = worldToCell(pos);

        auto it = m_Cells.find(cell);
        if (it == m_Cells.end()) {
            CellEntry entry{};
            entry.coord = cell;
            getCellBounds(cell, entry.boundsMin, entry.boundsMax);
            entry.state = WorldCellState::Unloaded;
            entry.priority = 0.0f;
            entry.lastAccessFrame = 0;
            entry.bIsDirty = true;
            entry.hlod0Index = -1;
            entry.hlod1Index = -1;
            entry.hlod0Ptr = nullptr;
            entry.hlod1Ptr = nullptr;
            entry.loadingFramesRemaining = 0;
            auto [newIt, inserted] = m_Cells.emplace(cell, std::move(entry));
            it = newIt;
        }
        it->second.entities.push_back(e);
        if (registry.all_of<::Mesh>(e)) {
            it->second.memoryBytes += estimateMeshEntityBytes(registry.get<::Mesh>(e));
        }
    }

    // Ensure bounds are set for all cells (already done on creation)
    for (auto& [coord, entry] : m_Cells) {
        // In case a cell was pre-existing before refactor, ensure coord/bounds are valid
        entry.coord = coord;
        if (glm::length(entry.boundsMax - entry.boundsMin) < 1e-3f) {
            getCellBounds(coord, entry.boundsMin, entry.boundsMax);
        }
    }

    // Assign priorities based on distance to streaming sources
    assignCellPriorities();

    m_Dirty = false;
}

void WorldPartition::assignCellPriorities() {
    recalculatePriorities();
}

glm::vec3 WorldPartition::getCameraPosition() const {
    if (!m_Scene) return glm::vec3(0.0f);

    auto& registry = m_Scene->getRegistry();

    // Try new ECS camera first (Atlas::ECS)
    if (auto view = registry.view<ECS::CameraComponent>(); view.begin() != view.end()) {
        auto camEntity = *view.begin();
        return registry.get<ECS::CameraComponent>(camEntity).position;
    }

    // Legacy cameras (global ::Camera / ::EditorCamera from ecs.h)
    if (auto view = registry.view<Camera>(); view.begin() != view.end()) {
        auto camEntity = *view.begin();
        return registry.get<Camera>(camEntity).position;
    }
    if (auto view = registry.view<EditorCamera>(); view.begin() != view.end()) {
        auto camEntity = *view.begin();
        return registry.get<EditorCamera>(camEntity).position;
    }

    // Fallback to scene active camera (if set) or GameCamera presence
    if (m_Scene->getActiveCamera()) {
        return m_Scene->getActiveCamera()->position;
    }
    if (auto view = registry.view<ECS::GameCameraComponent>(); view.begin() != view.end()) {
        return glm::vec3(0.0f, 2.0f, 5.0f);
    }

    return glm::vec3(0.0f);
}

void WorldPartition::setCellVisible(const WorldCellCoord& cell, bool visible) {
    auto it = m_Cells.find(cell);
    if (it == m_Cells.end()) return;
    if (!m_Scene) return;
    auto& registry = m_Scene->getRegistry();
    for (auto e : it->second.entities) {
        if (!registry.valid(e)) continue;
        if (registry.all_of<Renderable>(e)) {
            registry.get<Renderable>(e).visible = visible;
        }
        if (registry.all_of<ECS::RenderableComponent>(e)) {
            registry.get<ECS::RenderableComponent>(e).visible = visible;
        }
    }
}

// ---------------------------------------------------------------------------
// Legacy entity streaming sources (compat)
// ---------------------------------------------------------------------------
void WorldPartition::addStreamingSource(entt::entity entity) {
    if (!m_Scene) return;

    auto& registry = m_Scene->getRegistry();
    if (!registry.all_of<Transform>(entity)) return;

    m_StreamingSources.push_back({entity, glm::vec3(0.0f), 0.0f});
}

void WorldPartition::removeStreamingSource(entt::entity entity) {
    auto it = std::find_if(m_StreamingSources.begin(), m_StreamingSources.end(),
        [&](const EntityStreamingSource& src) { return src.entity == entity; });

    if (it != m_StreamingSources.end()) {
        m_StreamingSources.erase(it);
    }
}

void WorldPartition::updateStreamingSources(const glm::vec3* positions, uint32_t count, float deltaTime) {
    if (!m_Scene) return;

    for (size_t i = 0; i < std::min<size_t>(count, m_StreamingSources.size()); ++i) {
        m_StreamingSources[i].lastPosition = positions[i];
        m_StreamingSources[i].lastUpdateFrame = deltaTime;
    }

    recalculatePriorities();
}

// ---------------------------------------------------------------------------
// TDD §3.5 - Position-based streaming sources
// ---------------------------------------------------------------------------
size_t WorldPartition::addStreamingSource(const StreamingSource& source) {
    m_Sources.push_back(source);
    return m_Sources.size() - 1;
}

size_t WorldPartition::addStreamingSource(const glm::vec3& position, float priorityWeight, float radius) {
    StreamingSource s;
    s.position = position;
    s.priorityWeight = priorityWeight;
    s.radius = radius;
    return addStreamingSource(s);
}

bool WorldPartition::removeStreamingSource(size_t index) {
    if (index >= m_Sources.size()) return false;
    m_Sources.erase(m_Sources.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void WorldPartition::clearSources() {
    m_Sources.clear();
}

bool WorldPartition::updateSource(size_t index, const glm::vec3& newPosition) {
    if (index >= m_Sources.size()) return false;
    m_Sources[index].position = newPosition;
    return true;
}

bool WorldPartition::updateSource(size_t index, const StreamingSource& source) {
    if (index >= m_Sources.size()) return false;
    m_Sources[index] = source;
    return true;
}

const StreamingSource* WorldPartition::getSource(size_t index) const {
    if (index >= m_Sources.size()) return nullptr;
    return &m_Sources[index];
}

bool WorldPartition::getCellInfo(const WorldCellCoord& coord, WorldCell& out) const {
    auto it = m_Cells.find(coord);
    if (it == m_Cells.end()) return false;
    const auto& e = it->second;
    out.gridCoord = e.coord;
    out.boundsMin = e.boundsMin;
    out.boundsMax = e.boundsMax;
    out.state = e.state;
    out.priority = e.priority;
    out.entities = e.entities;
    out.hlod0Index = e.hlod0Index;
    out.hlod1Index = e.hlod1Index;
    out.hlod0Ptr = e.hlod0Ptr;
    out.hlod1Ptr = e.hlod1Ptr;
    out.lastAccessFrame = e.lastAccessFrame;
    out.bIsDirty = e.bIsDirty;
    out.loadingFramesRemaining = e.loadingFramesRemaining;
    out.memoryBytes = e.memoryBytes;
    return true;
}

void WorldPartition::recalculatePriorities() {
    for (auto& [coord, entry] : m_Cells) {
        glm::vec3 center = getCellCenter(coord);
        // Determine if this cell would be preload-only (outside loadRange but inside preload)
        float w, r;
        float raw = distanceToNearestSource(center, w, r);
        float eff = effectiveDistance(raw, w, r);
        float loadRange = m_Config.loadRangeMeters;
        float preloadExtra = m_Config.preloadDistanceCells * m_Config.cellSize;
        bool isPreloadOnly = (eff >= loadRange && eff < (loadRange + preloadExtra));
        entry.priority = computePriority(center, isPreloadOnly);
        // hysteresis bias: loaded cells keep slightly higher priority
        if (entry.state == WorldCellState::Loaded) {
            entry.priority = std::min(1.0f, entry.priority + 0.05f);
        }
    }
}

// ---------------------------------------------------------------------------
// Update cell states with hysteresis + budget + priority
// ---------------------------------------------------------------------------
void WorldPartition::updateCellStates(const glm::vec3& cameraPos, float /*cameraMoveDistanceThisFrame*/) {
    m_CurrentFrame++;

    if (m_Cells.empty()) return;

    const float hysteresisExtra = static_cast<float>(m_Config.hysteresisCells) * m_Config.cellSize;
    const float preloadExtra = static_cast<float>(m_Config.preloadDistanceCells) * m_Config.cellSize;
    const float loadRange = m_Config.loadRangeMeters;

    // First pass: compute priorities/distances and drive Unloaded->Loading / Loaded->Unloaded decisions
    std::vector<WorldCellCoord> toUnload;
    toUnload.reserve(32);

    for (auto& [coord, entry] : m_Cells) {
        glm::vec3 center = getCellCenter(coord);

        // Closest effective distance among all sources + cameraPos
        float bestRaw = std::numeric_limits<float>::max();
        float bestEff = std::numeric_limits<float>::max();
        float bestW = 1.0f;
        float bestR = 0.0f;

        // Position sources
        for (const auto& s : m_Sources) {
            float raw = glm::length(center - s.position);
            float eff = effectiveDistance(raw, s.priorityWeight, s.radius);
            if (eff < bestEff) {
                bestEff = eff;
                bestRaw = raw;
                bestW = s.priorityWeight;
                bestR = s.radius;
            }
        }
        // Legacy entity sources
        for (const auto& es : m_StreamingSources) {
            float raw = glm::length(center - es.lastPosition);
            float eff = raw;
            if (eff < bestEff) {
                bestEff = eff;
                bestRaw = raw;
                bestW = 1.0f;
                bestR = 0.0f;
            }
        }
        // Camera position as streaming source (always considered)
        {
            float raw = glm::length(center - cameraPos);
            float eff = raw;
            if (m_Sources.empty() && m_StreamingSources.empty()) {
                // No other sources: camera is the sole source
                bestEff = eff;
                bestRaw = raw;
                bestW = 1.0f;
                bestR = 0.0f;
            } else {
                if (eff < bestEff) {
                    bestEff = eff;
                    bestRaw = raw;
                    bestW = 1.0f;
                    bestR = 0.0f;
                }
            }
        }

        // Also fallback to getCameraPosition if caller passed zero and no sources? Already covered.

        float effDist = bestEff;
        (void)bestRaw;
        (void)bestW;
        (void)bestR;

        bool shouldLoad = effDist < loadRange;
        bool shouldKeepLoaded = effDist < (loadRange + hysteresisExtra);
        bool withinPreload = effDist < (loadRange + preloadExtra);
        bool isPreloadOnly = !shouldLoad && withinPreload;

        // Priority (weighted distance + hysteresis + preload penalty)
        float farDist = (m_Config.hlod1RangeMeters > 0.0f) ? m_Config.hlod1RangeMeters * 2.0f : 10000.0f;
        if (farDist < 10.0f) farDist = 10000.0f;
        float distFactor = 1.0f - std::min(effDist / farDist, 1.0f);
        float pri = distFactor * 0.9f + 0.1f;
        if (isPreloadOnly) pri *= 0.4f;
        if (entry.state == WorldCellState::Loaded) pri += 0.05f; // hysteresis priority boost
        entry.priority = std::clamp(pri, 0.0f, 1.0f);

        // State transitions (without budget)
        switch (entry.state) {
            case WorldCellState::Unloaded: {
                if (shouldLoad || isPreloadOnly) {
                    entry.state = WorldCellState::Loading;
                    entry.loadingFramesRemaining = std::max(1, m_Config.loadingFrames);
                    // TODO async real: if ThreadPool available, enqueue loading task instead of frame counter:
                    //   threadPool.enqueue([this, coord] { loadCellData(coord); });
                    // For now simulate async with N-frame delay.
                }
                break;
            }
            case WorldCellState::Loading: {
                if (!shouldLoad && !withinPreload) {
                    // Went far away before finishing loading -> cancel
                    entry.state = WorldCellState::Unloaded;
                    entry.loadingFramesRemaining = 0;
                }
                break;
            }
            case WorldCellState::Loaded: {
                // Hysteresis: only unload when beyond loadRange + hysteresis
                if (!shouldKeepLoaded && !withinPreload) {
                    toUnload.push_back(coord);
                } else {
                    entry.lastAccessFrame = m_CurrentFrame;
                }
                break;
            }
            case WorldCellState::Unloading: {
                entry.state = WorldCellState::Unloaded;
                entry.loadingFramesRemaining = 0;
                break;
            }
        }
    }

    // Process unloads immediately (cheap, no budget)
    for (const auto& c : toUnload) {
        auto it = m_Cells.find(c);
        if (it == m_Cells.end()) continue;
        it->second.state = WorldCellState::Unloaded;
        it->second.loadingFramesRemaining = 0;
        m_LoadedCells.erase(c);
        setCellVisible(c, false);
    }

    // TDD §8: enforce the memory budget by unloading lowest-priority cells.
    // Always keeps at least one loaded cell to avoid empty-world flicker.
    const float budgetMB = m_Config.maxLoadedMemoryMB;
    if (budgetMB > 0.0f && m_LoadedCells.size() > 1) {
        auto loadedBytes = [&]() -> uint64_t {
            uint64_t total = 0;
            for (const auto& c : m_LoadedCells) {
                auto it = m_Cells.find(c);
                if (it != m_Cells.end()) total += it->second.memoryBytes;
            }
            return total;
        };
        const uint64_t budgetBytes = static_cast<uint64_t>(budgetMB * 1024.0f * 1024.0f);
        if (loadedBytes() > budgetBytes) {
            std::vector<std::pair<float, WorldCellCoord>> byPriority;
            byPriority.reserve(m_LoadedCells.size());
            for (const auto& c : m_LoadedCells) {
                auto it = m_Cells.find(c);
                byPriority.emplace_back(it != m_Cells.end() ? it->second.priority : 0.0f, c);
            }
            std::sort(byPriority.begin(), byPriority.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [pri, c] : byPriority) {
                (void)pri;
                if (m_LoadedCells.size() <= 1 || loadedBytes() <= budgetBytes) break;
                auto it = m_Cells.find(c);
                if (it == m_Cells.end()) continue;
                it->second.state = WorldCellState::Unloaded;
                it->second.loadingFramesRemaining = 0;
                m_LoadedCells.erase(c);
                setCellVisible(c, false);
            }
        }
    }

    // Recompute the running total (immune to missed transition paths).
    m_LoadedMemoryBytes = 0;
    for (const auto& c : m_LoadedCells) {
        auto it = m_Cells.find(c);
        if (it != m_Cells.end()) m_LoadedMemoryBytes += it->second.memoryBytes;
    }

    // Collect Loading candidates whose frame counter expired
    struct Candidate { WorldCellCoord coord; float priority; };
    std::vector<Candidate> candidates;
    candidates.reserve(32);
    for (auto& [coord, entry] : m_Cells) {
        if (entry.state == WorldCellState::Loading) {
            if (entry.loadingFramesRemaining > 0) {
                entry.loadingFramesRemaining--;
            }
            if (entry.loadingFramesRemaining <= 0) {
                candidates.push_back({coord, entry.priority});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b){
        return a.priority > b.priority;
    });

    // Budget: max transitions per frame and time budget 2-4ms
    int maxTrans = m_Config.maxTransitionsPerFrame > 0 ? m_Config.maxTransitionsPerFrame : 2;
    float budgetMs = m_Config.budgetMs;
    if (budgetMs < 0.5f) budgetMs = 3.0f;
    if (budgetMs > 16.0f) budgetMs = 16.0f;

    auto start = std::chrono::high_resolution_clock::now();
    int transitioned = 0;
    for (const auto& cand : candidates) {
        if (transitioned >= maxTrans) break;
        auto now = std::chrono::high_resolution_clock::now();
        float elapsedMs = std::chrono::duration<float, std::milli>(now - start).count();
        if (elapsedMs > budgetMs) break;

        auto it = m_Cells.find(cand.coord);
        if (it == m_Cells.end()) continue;
        // Double-check still Loading (could have been cancelled)
        if (it->second.state != WorldCellState::Loading) continue;
        it->second.state = WorldCellState::Loaded;
        it->second.lastAccessFrame = m_CurrentFrame;
        it->second.bIsDirty = false;
        m_LoadedCells.insert(cand.coord);
        setCellVisible(cand.coord, true);
        transitioned++;
    }
}

// Full update method integrating cell state management and frustum culling
void WorldPartition::update(const glm::vec3& cameraPos, const glm::mat4& viewProj) {
    if (!m_Scene) return;
    if (!m_Config.enabled) {
        // Restore visibility for any cells we previously toggled.
        for (const auto& cell : m_LoadedCells) {
            setCellVisible(cell, true);
        }
        m_LoadedCells.clear();
        m_LoadedMemoryBytes = 0;
        return;
    }

    if (m_Dirty) {
        rebuild();
    }

    camCell = worldToCell(cameraPos);

    // Update cell states with hysteresis for smooth streaming
    updateCellStates(cameraPos, 0.0f);

    if (!m_Config.useFrustumCulling) return;

    // Frustum culling inside loaded cells only.
    Frustum fr = Frustum::fromViewProj(viewProj);

    auto& registry = m_Scene->getRegistry();

    for (const auto& cell : m_LoadedCells) {
        auto cit = m_Cells.find(cell);
        if (cit == m_Cells.end()) continue;

        for (auto e : cit->second.entities) {
            if (!registry.valid(e)) continue;
            if (!registry.all_of<::Mesh, Renderable>(e)) continue;
            auto& rend = registry.get<Renderable>(e);
            if (!rend.visible) continue;

            const auto& mesh = registry.get<::Mesh>(e);

            glm::mat4 world = m_Scene->getCachedWorldTransform(e);
            glm::vec3 worldPos = glm::vec3(world[3]);

            glm::vec3 center = worldPos;
            float radius = 1.0f;

            if (mesh.hasBounds) {
                glm::vec3 centerLocal = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
                glm::vec3 extents = (mesh.boundsMax - mesh.boundsMin) * 0.5f;

                glm::vec3 col0 = glm::vec3(world[0]);
                glm::vec3 col1 = glm::vec3(world[1]);
                glm::vec3 col2 = glm::vec3(world[2]);
                float maxScale = std::max(std::max(glm::length(col0), glm::length(col1)), glm::length(col2));

                center = glm::vec3(world * glm::vec4(centerLocal, 1.0f));
                radius = glm::length(extents) * maxScale;
            }

            if (!fr.testSphere(center, radius)) {
                rend.visible = false;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// TDD §9.1 cell metadata snapshot (bounds/counts/memory, no entities).
// Text format: header "ATLAS_CELLS 1" then one line per cell:
//   x z minX minY minZ maxX maxY maxZ entityCount memoryBytes
// ---------------------------------------------------------------------------
bool WorldPartition::saveCellIndex(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << "ATLAS_CELLS 1\n";
    out << m_Cells.size() << "\n";
    for (const auto& [coord, entry] : m_Cells) {
        out << coord.x << ' ' << coord.z
            << ' ' << entry.boundsMin.x << ' ' << entry.boundsMin.y << ' ' << entry.boundsMin.z
            << ' ' << entry.boundsMax.x << ' ' << entry.boundsMax.y << ' ' << entry.boundsMax.z
            << ' ' << entry.entities.size() << ' ' << entry.memoryBytes << '\n';
    }
    return static_cast<bool>(out);
}

size_t WorldPartition::loadCellIndex(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return 0;
    std::string magic;
    int version = 0;
    size_t count = 0;
    if (!(in >> magic >> version)) return 0;
    if (magic != "ATLAS_CELLS" || version != 1) return 0;
    if (!(in >> count)) return 0;
    size_t restored = 0;
    for (size_t i = 0; i < count; ++i) {
        WorldCellCoord coord{0, 0};
        glm::vec3 bmin(0.0f), bmax(0.0f);
        size_t entityCount = 0;
        uint64_t memoryBytes = 0;
        if (!(in >> coord.x >> coord.z
                  >> bmin.x >> bmin.y >> bmin.z
                  >> bmax.x >> bmax.y >> bmax.z
                  >> entityCount >> memoryBytes)) {
            break;
        }
        // Only fill metadata for cells not already resolved from entities;
        // rebuild() remains the source of truth when entities are present.
        auto it = m_Cells.find(coord);
        if (it == m_Cells.end()) {
            CellEntry entry{};
            entry.coord = coord;
            entry.boundsMin = bmin;
            entry.boundsMax = bmax;
            entry.state = WorldCellState::Unloaded;
            entry.memoryBytes = memoryBytes;
            entry.bIsDirty = true;
            m_Cells.emplace(coord, std::move(entry));
            restored++;
        }
    }
    return restored;
}

} // namespace Atlas
