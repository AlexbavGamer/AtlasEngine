#include "physics_system.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../ecs/ecs.h"
#include "../ecs/components/components.h"
#include "../scene/scene.h"
#include "../utils/model_loader.h"
#include "../utils/primitive_helpers.h"

// ---------------------------------------------------------------------------
// Atlas Physics: a small self-contained rigid-body simulation.
//
// Features:
//   - Static / Dynamic / Kinematic bodies
//   - Gravity, linear damping, restitution, friction (simplified)
//   - Colliders: box (OBB), sphere, capsule (approximated as a swept segment)
//   - Collision detection: sphere-sphere, sphere-box (closest point on OBB),
//     box-box (separating axis theorem), capsule-segment vs sphere/box
//   - Iterative position projection + velocity response
// ---------------------------------------------------------------------------

namespace Atlas::Physics {
namespace {

using PhysicsMotionType = ECS::PhysicsMotionType;

constexpr glm::vec3 kGravity = glm::vec3(0.0f, -9.81f, 0.0f);
constexpr float kMaxDeltaTime = 1.0f / 20.0f;   // clamp per-step to avoid tunneling
constexpr int kSolverIterations = 4;
constexpr float kSleepLinearThreshold = 0.08f;  // m/s
constexpr float kSleepAngularThreshold = 0.05f; // rad/s
constexpr float kSleepTime = 0.5f;              // seconds below thresholds

// Rotation is stored as Euler angles in DEGREES throughout the ECS (the rest
// of the engine converts with glm::radians when needed).
glm::quat rotationQuat(const glm::vec3& rotationDegrees) {
    return glm::quat(glm::radians(rotationDegrees));
}

glm::quat rotationQuat(const Transform& t) {
    return rotationQuat(t.rotation);
}

glm::vec3 rotatedOffset(const Transform& t, const glm::vec3& offset) {
    return glm::mat3_cast(rotationQuat(t)) * offset;
}

// ---------------------------------------------------------------------------
// Triangle-mesh collision data (V1: static triangle soup + median-split BVH).
// Built once per unique meshPath and shared across entities.
// ---------------------------------------------------------------------------
struct TriMeshBVHNode {
    glm::vec3 bmin{0.0f};
    glm::vec3 bmax{0.0f};
    int32_t left = -1;
    int32_t right = -1;
    uint32_t start = 0; // first triangle in triOrder (leaf only)
    uint32_t count = 0; // triangle count (leaf only, 0 = interior)
};

struct TriMesh {
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices; // 3 per triangle
    glm::vec3 bmin{0.0f};
    glm::vec3 bmax{0.0f};
    std::vector<TriMeshBVHNode> nodes;
    std::vector<uint32_t> triOrder; // triangle indices in leaf order
    int32_t root = -1;

    uint32_t triCount() const { return static_cast<uint32_t>(indices.size() / 3u); }
    bool empty() const { return verts.empty() || indices.size() < 3u; }
};

constexpr uint32_t kTriMeshLeafMaxTris = 8;
constexpr int32_t kTriMeshMaxDepth = 32;

static void triMeshBoundsOf(const TriMesh& mesh, uint32_t tri, glm::vec3& outMin, glm::vec3& outMax) {
    const glm::vec3& a = mesh.verts[mesh.indices[tri * 3u]];
    const glm::vec3& b = mesh.verts[mesh.indices[tri * 3u + 1u]];
    const glm::vec3& c = mesh.verts[mesh.indices[tri * 3u + 2u]];
    outMin = glm::min(a, glm::min(b, c));
    outMax = glm::max(a, glm::max(b, c));
}

static int32_t triMeshBuildNode(TriMesh& mesh, uint32_t* tris, uint32_t count, int32_t depth) {
    const int32_t nodeIdx = static_cast<int32_t>(mesh.nodes.size());
    mesh.nodes.emplace_back();
    TriMeshBVHNode& node = mesh.nodes.back();
    node.bmin = glm::vec3(std::numeric_limits<float>::max());
    node.bmax = glm::vec3(std::numeric_limits<float>::lowest());
    glm::vec3 cmin(std::numeric_limits<float>::max());
    glm::vec3 cmax(std::numeric_limits<float>::lowest());
    for (uint32_t i = 0; i < count; ++i) {
        glm::vec3 tmin, tmax;
        triMeshBoundsOf(mesh, tris[i], tmin, tmax);
        node.bmin = glm::min(node.bmin, tmin);
        node.bmax = glm::max(node.bmax, tmax);
        const glm::vec3 centroid = (tmin + tmax) * 0.5f;
        cmin = glm::min(cmin, centroid);
        cmax = glm::max(cmax, centroid);
    }
    if (count <= kTriMeshLeafMaxTris || depth >= kTriMeshMaxDepth) {
        node.start = static_cast<uint32_t>(mesh.triOrder.size());
        node.count = count;
        for (uint32_t i = 0; i < count; ++i) {
            mesh.triOrder.push_back(tris[i]);
        }
        return nodeIdx;
    }
    const glm::vec3 extent = cmax - cmin;
    int axis = 0;
    if (extent.y > extent.x && extent.y >= extent.z) axis = 1;
    else if (extent.z > extent.x && extent.z > extent.y) axis = 2;
    const uint32_t mid = count / 2u;
    std::nth_element(tris, tris + mid, tris + count, [&](uint32_t ta, uint32_t tb) {
        glm::vec3 amn, amx, bmn, bmx;
        triMeshBoundsOf(mesh, ta, amn, amx);
        triMeshBoundsOf(mesh, tb, bmn, bmx);
        return ((amn + amx) * 0.5f)[axis] < ((bmn + bmx) * 0.5f)[axis];
    });
    if (mid == 0 || mid >= count) { // degenerate split: force leaf
        node.start = static_cast<uint32_t>(mesh.triOrder.size());
        node.count = count;
        for (uint32_t i = 0; i < count; ++i) {
            mesh.triOrder.push_back(tris[i]);
        }
        return nodeIdx;
    }
    // NOTE: node is a reference into mesh.nodes; recursion may reallocate the
    // vector, so re-fetch it after building children.
    const int32_t left = triMeshBuildNode(mesh, tris, mid, depth + 1);
    const int32_t right = triMeshBuildNode(mesh, tris + mid, count - mid, depth + 1);
    mesh.nodes[static_cast<size_t>(nodeIdx)].left = left;
    mesh.nodes[static_cast<size_t>(nodeIdx)].right = right;
    return nodeIdx;
}

static void triMeshBuildBVH(TriMesh& mesh) {
    mesh.nodes.clear();
    mesh.triOrder.clear();
    mesh.nodes.reserve(mesh.triCount() * 2u + 1u);
    mesh.triOrder.reserve(mesh.triCount());
    const uint32_t count = mesh.triCount();
    if (count == 0) {
        mesh.root = -1;
        return;
    }
    std::vector<uint32_t> tris(count);
    for (uint32_t i = 0; i < count; ++i) tris[i] = i;
    mesh.root = triMeshBuildNode(mesh, tris.data(), count, 0);
}

struct Collider {
    enum class Kind : uint8_t { Box, Sphere, Capsule, Mesh };
    // False when the collider could not be built (e.g. mesh file missing).
    // Callers must skip invalid colliders instead of using defaults.
    bool valid = true;
    Kind kind = Kind::Sphere;
    glm::vec3 halfExtent = glm::vec3(0.5f); // box
    float radius = 0.5f;                    // sphere / capsule
    float halfHeight = 0.5f;                // capsule
    glm::vec3 offset = glm::vec3(0.0f);
    bool isTrigger = false;
    // Mesh data (Kind::Mesh only). Shared per meshPath via Impl::meshCache.
    std::shared_ptr<TriMesh> triMesh;
    glm::vec3 meshBoundsMin{0.0f}; // local-space bounds (fallback + proxy)
    glm::vec3 meshBoundsMax{0.0f};
    bool meshFallbackToBox = false; // dynamic bodies: OBB approximation
};

using MeshCache = std::unordered_map<std::string, std::shared_ptr<TriMesh>>;

// Build collision triangles for a mesh path. Primitives are generated
// procedurally; file meshes are reloaded CPU-only (null device keeps
// vertices) and the submesh matching the '#name' suffix is extracted.
static std::shared_ptr<TriMesh> loadTriMeshForPath(const std::string& meshPath) {
    auto out = std::make_shared<TriMesh>();
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;

    if (meshPath.rfind("primitive://", 0) == 0) {
        const std::string prim = meshPath.substr(sizeof("primitive://") - 1u);
        MeshData data;
        if (prim == "Cube" || prim == "TestCityBox") {
            data = ModelLoader::createCube(1.0f);
        } else if (prim == "Plane") {
            data = Atlas::PrimitiveHelpers::createPlane(2.0f);
        } else if (prim == "Sphere") {
            data = Atlas::PrimitiveHelpers::createSphere(0.5f);
        } else if (prim == "Cylinder") {
            data = Atlas::PrimitiveHelpers::createCylinder(0.5f, 1.5f);
        } else if (prim == "Capsule") {
            data = Atlas::PrimitiveHelpers::createCapsule(0.45f, 1.8f);
        } else {
            std::cerr << "[Physics] MeshCollider: unknown primitive '" << meshPath << "'" << std::endl;
            return nullptr;
        }
        positions.reserve(data.vertices.size());
        for (const auto& v : data.vertices) positions.push_back(v.pos);
        indices = data.indices;
    } else {
        std::string filePath = meshPath;
        std::string subName;
        const size_t hashPos = meshPath.rfind('#');
        if (hashPos != std::string::npos) {
            filePath = meshPath.substr(0, hashPos);
            subName = meshPath.substr(hashPos + 1u);
        }
        try {
            ModelData modelData;
            ModelLoader::loadModelMultiMesh(filePath, VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr,
                                             &modelData, false, false);
            const MeshData* picked = nullptr;
            if (!subName.empty()) {
                for (const auto& m : modelData.meshes) {
                    if (m.name == subName) { picked = &m; break; }
                }
            }
            if (!picked && !modelData.meshes.empty() && subName.empty()) {
                picked = &modelData.meshes.front();
            }
            if (!picked) {
                std::cerr << "[Physics] MeshCollider: submesh '" << subName
                          << "' not found in '" << filePath << "'" << std::endl;
                return nullptr;
            }
            positions.reserve(picked->vertices.size());
            for (const auto& v : picked->vertices) positions.push_back(v.pos);
            indices = picked->indices;
        } catch (const std::exception& e) {
            std::cerr << "[Physics] MeshCollider: failed to load '" << filePath << "': " << e.what() << std::endl;
            return nullptr;
        }
    }

    if (positions.empty() || indices.size() < 3u) {
        std::cerr << "[Physics] MeshCollider: no triangles for '" << meshPath << "'" << std::endl;
        return nullptr;
    }
    out->verts = std::move(positions);
    out->indices = std::move(indices);
    out->bmin = glm::vec3(std::numeric_limits<float>::max());
    out->bmax = glm::vec3(std::numeric_limits<float>::lowest());
    for (const auto& v : out->verts) {
        out->bmin = glm::min(out->bmin, v);
        out->bmax = glm::max(out->bmax, v);
    }
    triMeshBuildBVH(*out);
    return out;
}

Collider colliderFromEntity(entt::registry& registry, entt::entity entity, MeshCache& meshCache) {
    Collider c;
    if (registry.all_of<ECS::BoxColliderComponent>(entity)) {
        const auto& comp = registry.get<ECS::BoxColliderComponent>(entity);
        c.kind = Collider::Kind::Box;
        c.halfExtent = glm::max(comp.halfExtent, glm::vec3(0.01f));
        c.offset = comp.offset;
        c.isTrigger = comp.isTrigger;
    } else if (registry.all_of<ECS::SphereColliderComponent>(entity)) {
        const auto& comp = registry.get<ECS::SphereColliderComponent>(entity);
        c.kind = Collider::Kind::Sphere;
        c.radius = std::max(0.01f, comp.radius);
        c.offset = comp.offset;
        c.isTrigger = comp.isTrigger;
    } else if (registry.all_of<ECS::CapsuleColliderComponent>(entity)) {
        const auto& comp = registry.get<ECS::CapsuleColliderComponent>(entity);
        c.kind = Collider::Kind::Capsule;
        c.radius = std::max(0.01f, comp.radius);
        c.halfHeight = std::max(0.01f, comp.halfHeight);
        c.offset = comp.offset;
        c.isTrigger = comp.isTrigger;
    } else if (registry.all_of<ECS::MeshColliderComponent>(entity)) {
        // Explicit primitives win: mesh is the last resort.
        if (!registry.all_of<::Mesh>(entity)) {
            return c;
        }
        const auto& comp = registry.get<ECS::MeshColliderComponent>(entity);
        const auto& mesh = registry.get<::Mesh>(entity);
        if (mesh.meshPath.empty()) {
            return c;
        }
        auto it = meshCache.find(mesh.meshPath);
        if (it == meshCache.end()) {
            auto triMesh = loadTriMeshForPath(mesh.meshPath);
            if (!triMesh) {
                c.valid = false;
                return c;
            }
            it = meshCache.emplace(mesh.meshPath, std::move(triMesh)).first;
        }
        c.kind = Collider::Kind::Mesh;
        c.triMesh = it->second;
        c.meshBoundsMin = it->second->bmin;
        c.meshBoundsMax = it->second->bmax;
        c.offset = comp.offset;
        c.isTrigger = comp.isTrigger;
        // Concave dynamics need convex decomposition (out of scope V1):
        // dynamic mesh bodies collide as their OBB.
        if (registry.all_of<ECS::RigidBodyComponent>(entity) &&
            registry.get<ECS::RigidBodyComponent>(entity).motionType == PhysicsMotionType::Dynamic) {
            c.meshFallbackToBox = true;
            std::cerr << "[Physics] MeshCollider on dynamic body: OBB fallback (entity "
                      << static_cast<uint32_t>(entity) << ")" << std::endl;
        }
    }
    return c;
}

// ---------------------------------------------------------------------------
// Collision queries
// ---------------------------------------------------------------------------

// Closest point on an OBB to a given point, expressed in world space.
glm::vec3 closestPointOnBox(const glm::vec3& point,
                            const glm::vec3& center,
                            const glm::quat& rot,
                            const glm::vec3& halfExtent) {
    const glm::mat3 basis = glm::mat3_cast(rot);
    const glm::vec3 local = glm::inverse(basis) * (point - center);
    const glm::vec3 clamped = glm::clamp(local, -halfExtent, halfExtent);
    return center + basis * clamped;
}

struct Contact {
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float penetration = 0.0f;
    bool hit = false;
};

// Sphere vs OBB: returns contact normal (pointing from box toward sphere).
Contact sphereBoxContact(const glm::vec3& sphereCenter, float sphereRadius,
                         const glm::vec3& boxCenter, const glm::quat& boxRot,
                         const glm::vec3& halfExtent) {
    Contact c;
    const glm::vec3 closest = closestPointOnBox(sphereCenter, boxCenter, boxRot, halfExtent);
    const glm::vec3 delta = sphereCenter - closest;
    const float distSq = glm::dot(delta, delta);
    if (distSq >= sphereRadius * sphereRadius) {
        return c; // no contact
    }
    c.hit = true;
    const float dist = std::sqrt(distSq);
    if (dist > 1e-6f) {
        c.normal = delta / dist;
        c.penetration = sphereRadius - dist;
    } else {
        // Sphere center inside the box: push out along the smallest axis.
        const glm::mat3 basis = glm::mat3_cast(boxRot);
        const glm::vec3 local = glm::inverse(basis) * (sphereCenter - boxCenter);
        const glm::vec3 d = halfExtent - glm::abs(local);
        const float minAxis = std::min({d.x, d.y, d.z});
        glm::vec3 axisLocal(0.0f);
        if (minAxis == d.x) axisLocal.x = (local.x < 0.0f ? -1.0f : 1.0f);
        else if (minAxis == d.y) axisLocal.y = (local.y < 0.0f ? -1.0f : 1.0f);
        else axisLocal.z = (local.z < 0.0f ? -1.0f : 1.0f);
        c.normal = glm::normalize(basis * axisLocal);
        c.penetration = minAxis + sphereRadius;
    }
    return c;
}

// Sphere vs sphere.
Contact sphereSphereContact(const glm::vec3& aCenter, float aRadius,
                            const glm::vec3& bCenter, float bRadius) {
    Contact c;
    const glm::vec3 delta = aCenter - bCenter;
    const float r = aRadius + bRadius;
    const float distSq = glm::dot(delta, delta);
    if (distSq >= r * r) {
        return c;
    }
    c.hit = true;
    const float dist = std::sqrt(distSq);
    if (dist > 1e-6f) {
        c.normal = delta / dist;
        c.penetration = r - dist;
    } else {
        c.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        c.penetration = r;
    }
    return c;
}

// Capsule axis (world space) given rotation and half height (local Y axis).
void capsuleSegment(const glm::vec3& center, const glm::quat& rot,
                    float halfHeight, glm::vec3& a, glm::vec3& b) {
    const glm::vec3 axis = glm::normalize(glm::mat3_cast(rot) * glm::vec3(0.0f, 1.0f, 0.0f));
    a = center - axis * halfHeight;
    b = center + axis * halfHeight;
}

float closestPointOnSegment(const glm::vec3& p, const glm::vec3& a,
                            const glm::vec3& b, glm::vec3& out) {
    const glm::vec3 ab = b - a;
    const float lenSq = glm::dot(ab, ab);
    if (lenSq < 1e-8f) {
        out = a;
        return glm::length(p - a);
    }
    const float t = glm::clamp(glm::dot(p - a, ab) / lenSq, 0.0f, 1.0f);
    out = a + ab * t;
    return glm::length(p - out);
}

// Capsule (swept segment) vs sphere.
Contact capsuleSphereContact(const glm::vec3& capCenter, const glm::quat& capRot,
                             float capRadius, float capHalfHeight,
                             const glm::vec3& sphereCenter, float sphereRadius) {
    Contact c;
    glm::vec3 a, b;
    capsuleSegment(capCenter, capRot, capHalfHeight, a, b);
    glm::vec3 closest;
    const float dist = closestPointOnSegment(sphereCenter, a, b, closest);
    const float r = capRadius + sphereRadius;
    if (dist >= r) {
        return c;
    }
    c.hit = true;
    const glm::vec3 delta = sphereCenter - closest;
    if (dist > 1e-6f) {
        c.normal = delta / dist;
        c.penetration = r - dist;
    } else {
        // Sphere center exactly on the segment: pick a fallback normal.
        const glm::vec3 axis = glm::normalize(glm::mat3_cast(capRot) * glm::vec3(0.0f, 1.0f, 0.0f));
        c.normal = std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        c.penetration = r;
    }
    return c;
}

// Capsule vs OBB: test the swept segment against the box via closest point.
Contact capsuleBoxContact(const glm::vec3& capCenter, const glm::quat& capRot,
                          float capRadius, float capHalfHeight,
                          const glm::vec3& boxCenter, const glm::quat& boxRot,
                          const glm::vec3& halfExtent) {
    Contact c;
    glm::vec3 a, b;
    capsuleSegment(capCenter, capRot, capHalfHeight, a, b);

    // Sample the segment: closest point on segment to box center, plus
    // sphere-box tests at both endpoints and the closest point for a good
    // approximation.
    const glm::vec3 points[] = {a, b};
    Contact best;
    float bestPen = -1.0f;
    for (const glm::vec3& p : points) {
        Contact cc = sphereBoxContact(p, capRadius, boxCenter, boxRot, halfExtent);
        if (cc.hit && cc.penetration > bestPen) {
            bestPen = cc.penetration;
            best = cc;
        }
    }
    // Also test the closest point of the segment to the box center.
    glm::vec3 closestOnSeg;
    closestPointOnSegment(boxCenter, a, b, closestOnSeg);
    Contact cc = sphereBoxContact(closestOnSeg, capRadius, boxCenter, boxRot, halfExtent);
    if (cc.hit && cc.penetration > bestPen) {
        bestPen = cc.penetration;
        best = cc;
    }
    return best;
}

// OBB vs OBB via separating axis theorem. Returns MTV axis + depth.
Contact boxBoxContact(const glm::vec3& aCenter, const glm::quat& aRot, const glm::vec3& aHalf,
                      const glm::vec3& bCenter, const glm::quat& bRot, const glm::vec3& bHalf) {
    Contact c;

    const glm::mat3 a = glm::mat3_cast(aRot);
    const glm::mat3 b = glm::mat3_cast(bRot);

    const glm::vec3 aAxes[3] = {a[0], a[1], a[2]};
    const glm::vec3 bAxes[3] = {b[0], b[1], b[2]};

    glm::vec3 axisCandidates[15];
    int axisCount = 0;
    for (int i = 0; i < 3; ++i) axisCandidates[axisCount++] = aAxes[i];
    for (int i = 0; i < 3; ++i) axisCandidates[axisCount++] = bAxes[i];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            axisCandidates[axisCount++] = glm::cross(aAxes[i], bAxes[j]);
        }
    }

    const glm::vec3 delta = aCenter - bCenter;

    float minOverlap = std::numeric_limits<float>::max();
    glm::vec3 mtvAxis(0.0f, 1.0f, 0.0f);

    for (int i = 0; i < axisCount; ++i) {
        glm::vec3 axis = axisCandidates[i];
        const float lenSq = glm::dot(axis, axis);
        if (lenSq < 1e-8f) {
            continue;
        }
        axis /= std::sqrt(lenSq);

        // Project both boxes onto the axis.
        float aRadius = std::abs(glm::dot(aAxes[0] * aHalf.x, axis)) +
                        std::abs(glm::dot(aAxes[1] * aHalf.y, axis)) +
                        std::abs(glm::dot(aAxes[2] * aHalf.z, axis));
        float bRadius = std::abs(glm::dot(bAxes[0] * bHalf.x, axis)) +
                        std::abs(glm::dot(bAxes[1] * bHalf.y, axis)) +
                        std::abs(glm::dot(bAxes[2] * bHalf.z, axis));
        const float centerDist = std::abs(glm::dot(delta, axis));
        const float overlap = aRadius + bRadius - centerDist;
        if (overlap <= 0.0f) {
            return c; // separated
        }
        if (overlap < minOverlap) {
            minOverlap = overlap;
            mtvAxis = axis;
        }
    }

    // Flip the MTV axis to point from B toward A.
    if (glm::dot(mtvAxis, delta) < 0.0f) {
        mtvAxis = -mtvAxis;
    }
    c.hit = true;
    c.normal = mtvAxis;
    c.penetration = minOverlap;
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

struct BodyState {
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 angularVelocity = glm::vec3(0.0f);
    bool sleeping = false;
    float sleepTimer = 0.0f;
};

struct PhysicsSystem::Impl {
    bool initialized = false;
    Scene* scene = nullptr;

    std::unordered_map<entt::entity, BodyState> bodies;
    std::unordered_map<entt::entity, Collider> colliders;
    // CPU triangle data per unique meshPath (mesh colliders only).
    MeshCache meshCache;

    void clearBodies() {
        bodies.clear();
        colliders.clear();
    }
};

PhysicsSystem::PhysicsSystem() : m_Impl(std::make_unique<Impl>()) {}

PhysicsSystem::~PhysicsSystem() {
    shutdown();
}

bool PhysicsSystem::initialize() {
    if (m_Impl->initialized) {
        return true;
    }
    m_Impl->initialized = true;
    return true;
}

void PhysicsSystem::shutdown() {
    if (!m_Impl || !m_Impl->initialized) {
        return;
    }
    clear();
    m_Impl->initialized = false;
}

void PhysicsSystem::clear() {
    if (!m_Impl) {
        return;
    }
    m_Impl->clearBodies();
    m_Impl->scene = nullptr;
}

void PhysicsSystem::rebuild(Scene* scene) {
    if (!initialize()) {
        return;
    }

    clear();
    m_Impl->scene = scene;
    if (!scene) {
        return;
    }

    auto& registry = scene->getRegistry();
    for (entt::entity entity : scene->getAllEntities()) {
        if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) {
            continue;
        }
        if (registry.all_of<ECS::EditorHiddenComponent>(entity) &&
            registry.get<ECS::EditorHiddenComponent>(entity).hidden) {
            continue;
        }

        Collider collider = colliderFromEntity(registry, entity, m_Impl->meshCache);
        const bool hasCollider =
            registry.all_of<ECS::BoxColliderComponent>(entity) ||
            registry.all_of<ECS::SphereColliderComponent>(entity) ||
            registry.all_of<ECS::CapsuleColliderComponent>(entity) ||
            registry.all_of<ECS::MeshColliderComponent>(entity);
        if (!hasCollider || !collider.valid) {
            continue;
        }

        BodyState state;
        if (registry.all_of<ECS::RigidBodyComponent>(entity)) {
            const auto& rb = registry.get<ECS::RigidBodyComponent>(entity);
            state.velocity = glm::vec3(0.0f);
            state.angularVelocity = glm::vec3(0.0f);
            state.sleeping = rb.motionType != PhysicsMotionType::Dynamic;
        }
        m_Impl->bodies.emplace(entity, state);
        m_Impl->colliders.emplace(entity, std::move(collider));
    }
}

namespace {

// Build the world-space collision proxy for an entity.
struct Proxy {
    Collider::Kind kind;
    glm::vec3 center;
    glm::quat rot;
    glm::vec3 halfExtent;
    float radius;
    float halfHeight;
    // Mesh data (kind == Mesh only): effective local->world matrix (offset
    // baked into translation), scale extremes for conservative conversion.
    std::shared_ptr<TriMesh> triMesh;
    glm::mat4 meshMatrix{1.0f};
    float meshMinScale = 1.0f;
    float meshMaxScale = 1.0f;
};

Proxy makeProxy(const Transform& t, const Collider& c) {
    Proxy p;
    p.kind = c.kind;
    p.center = t.position + rotatedOffset(t, c.offset);
    p.rot = rotationQuat(t);
    p.halfExtent = c.halfExtent;
    p.radius = c.radius;
    p.halfHeight = c.halfHeight;
    if (c.kind == Collider::Kind::Mesh) {
        if (c.meshFallbackToBox || !c.triMesh) {
            // OBB approximation from local mesh bounds (includes entity scale).
            p.kind = Collider::Kind::Box;
            const glm::vec3 centerLocal = (c.meshBoundsMin + c.meshBoundsMax) * 0.5f;
            const glm::vec3 extLocal = (c.meshBoundsMax - c.meshBoundsMin) * 0.5f;
            p.halfExtent = extLocal * t.scale;
            p.center = t.position + rotatedOffset(t, c.offset) +
                       glm::mat3_cast(p.rot) * (centerLocal * t.scale);
        } else {
            p.triMesh = c.triMesh;
            const glm::mat4 m = glm::translate(glm::mat4(1.0f), t.position + rotatedOffset(t, c.offset)) *
                                glm::mat4_cast(p.rot) * glm::scale(glm::mat4(1.0f), t.scale);
            p.meshMatrix = m;
            const float sx = glm::length(glm::vec3(m[0]));
            const float sy = glm::length(glm::vec3(m[1]));
            const float sz = glm::length(glm::vec3(m[2]));
            p.meshMinScale = std::max(1e-6f, std::min({sx, sy, sz}));
            p.meshMaxScale = std::max({sx, sy, sz, 1e-6f});
            // World center of the mesh bounds for broadphase use.
            const glm::vec3 centerLocal = (c.meshBoundsMin + c.meshBoundsMax) * 0.5f;
            p.center = glm::vec3(m * glm::vec4(centerLocal, 1.0f));
            const glm::vec3 extLocal = (c.meshBoundsMax - c.meshBoundsMin) * 0.5f;
            p.halfExtent = extLocal * t.scale;
        }
    }
    return p;
}

// ---------------------------------------------------------------------------
// Triangle-mesh narrowphase (mesh is always the reference side).
// Conventions match the rest of this file: returned normal points from the
// mesh toward the primitive. Callers negate when the primitive is `a`.
// Non-uniform entity scale is handled conservatively (radii divided by the
// minimum basis length so contacts are never missed; penetration is scaled
// back by the maximum basis length).
// ---------------------------------------------------------------------------

// Closest point on triangle (Ericson 5.1.5).
glm::vec3 closestPointTriangle(const glm::vec3& p, const glm::vec3& a,
                               const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return a + ab * v;
    }
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return a + ac * w;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w;
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return a + ab * v + ac * w;
}

float distSqPointAABB(const glm::vec3& p, const glm::vec3& bmin, const glm::vec3& bmax) {
    const glm::vec3 clamped = glm::clamp(p, bmin, bmax);
    const glm::vec3 d = p - clamped;
    return glm::dot(d, d);
}

struct TriQueryBest {
    float distSq = std::numeric_limits<float>::max();
    glm::vec3 closest{0.0f};
    glm::vec3 faceNormal{0.0f, 1.0f, 0.0f};
    bool found = false;
};

void triMeshQuerySphere(const TriMesh& mesh, int32_t nodeIdx, const glm::vec3& center,
                        float radius, TriQueryBest& best) {
    if (nodeIdx < 0) return;
    const TriMeshBVHNode& node = mesh.nodes[static_cast<size_t>(nodeIdx)];
    const float rPad = radius;
    glm::vec3 clamped = glm::clamp(center, node.bmin, node.bmax);
    glm::vec3 diff = center - clamped;
    if (glm::dot(diff, diff) > (best.distSq + rPad * rPad + 2.0f * std::sqrt(best.distSq) * rPad) && best.found) {
        // Node farther than best possible improvement (with radius padding).
    }
    // Simpler correct prune: skip only if node is farther than bestDist + radius.
    const float nodeDistSq = distSqPointAABB(center, node.bmin, node.bmax);
    const float bestDist = best.found ? std::sqrt(best.distSq) : std::numeric_limits<float>::max();
    if (best.found && nodeDistSq > (bestDist + radius) * (bestDist + radius)) {
        return;
    }
    if (node.count > 0) {
        for (uint32_t i = 0; i < node.count; ++i) {
            const uint32_t tri = mesh.triOrder[node.start + i];
            const glm::vec3& a = mesh.verts[mesh.indices[tri * 3u]];
            const glm::vec3& b = mesh.verts[mesh.indices[tri * 3u + 1u]];
            const glm::vec3& cc = mesh.verts[mesh.indices[tri * 3u + 2u]];
            const glm::vec3 q = closestPointTriangle(center, a, b, cc);
            const glm::vec3 dd = center - q;
            const float dsq = glm::dot(dd, dd);
            if (dsq < best.distSq) {
                best.distSq = dsq;
                best.closest = q;
                glm::vec3 fn = glm::cross(b - a, cc - a);
                best.faceNormal = glm::length(fn) > 1e-12f ? glm::normalize(fn) : glm::vec3(0.0f, 1.0f, 0.0f);
                best.found = true;
            }
        }
        return;
    }
    triMeshQuerySphere(mesh, node.left, center, radius, best);
    triMeshQuerySphere(mesh, node.right, center, radius, best);
}

// Sphere (world) vs exact triangle mesh proxy. Normal: mesh -> sphere.
Contact sphereTriMeshContact(const glm::vec3& sphereCenter, float sphereRadius, const Proxy& meshProxy) {
    Contact c;
    const TriMesh& mesh = *meshProxy.triMesh;
    const glm::mat4 invM = glm::inverse(meshProxy.meshMatrix);
    const glm::vec3 cLocal = glm::vec3(invM * glm::vec4(sphereCenter, 1.0f));
    const float rLocal = sphereRadius / meshProxy.meshMinScale;
    TriQueryBest best;
    triMeshQuerySphere(mesh, mesh.root, cLocal, rLocal, best);
    if (!best.found) return c;
    const float dist = std::sqrt(best.distSq);
    if (dist >= rLocal) return c;
    c.hit = true;
    glm::vec3 nLocal;
    if (dist > 1e-6f) {
        nLocal = (cLocal - best.closest) / dist;
    } else {
        nLocal = best.faceNormal;
        if (glm::dot(nLocal, cLocal - best.closest) < 0.0f) nLocal = -nLocal;
    }
    c.normal = glm::normalize(meshProxy.rot * nLocal);
    c.penetration = (rLocal - dist) * meshProxy.meshMaxScale;
    return c;
}

// Capsule vs mesh: endpoint + midpoint spheres, deepest wins (same sampling
// philosophy as the existing capsuleBoxContact).
Contact capsuleTriMeshContact(const glm::vec3& capCenter, const glm::quat& capRot,
                              float capRadius, float capHalfHeight, const Proxy& meshProxy) {
    glm::vec3 a, b;
    capsuleSegment(capCenter, capRot, capHalfHeight, a, b);
    const glm::vec3 mid = (a + b) * 0.5f;
    Contact best;
    float bestPen = -1.0f;
    const glm::vec3 pts[] = {a, b, mid};
    for (const glm::vec3& p : pts) {
        Contact cc = sphereTriMeshContact(p, capRadius, meshProxy);
        if (cc.hit && cc.penetration > bestPen) {
            bestPen = cc.penetration;
            best = cc;
        }
    }
    return best;
}

// Box vs mesh: box corners as spheres + mesh vertices inside the box.
// Covers face presses and spikes; thin edge-edge crossings without enclosed
// vertices may be missed (documented V1 limit).
Contact boxTriMeshContact(const glm::vec3& boxCenter, const glm::quat& boxRot,
                          const glm::vec3& halfExtent, const Proxy& meshProxy) {
    Contact best;
    float bestPen = -1.0f;
    const TriMesh& mesh = *meshProxy.triMesh;
    const glm::mat4 invM = glm::inverse(meshProxy.meshMatrix);
    const glm::mat4 invBox = glm::inverse(glm::translate(glm::mat4(1.0f), boxCenter) *
                                           glm::mat4_cast(boxRot));
    // (1) Box corners vs triangles.
    static const glm::vec3 kCornerSign[8] = {
        {-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
        {-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1},
    };
    const float cornerR = 0.25f * std::max(0.01f, std::min({halfExtent.x, halfExtent.y, halfExtent.z}));
    const glm::mat3 boxBasis = glm::mat3_cast(boxRot);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 cornerLocal = kCornerSign[i] * halfExtent;
        const glm::vec3 cornerWorld = boxCenter + boxBasis * cornerLocal;
        Contact cc = sphereTriMeshContact(cornerWorld, cornerR, meshProxy);
        if (cc.hit && cc.penetration > bestPen) {
            bestPen = cc.penetration;
            best = cc;
        }
    }
    // (2) Mesh triangle vertices inside the box (BVH box query in mesh space).
    // Gather candidate triangles whose bounds overlap the box-in-mesh-space AABB.
    glm::vec3 boxCornersW[8];
    for (int i = 0; i < 8; ++i) {
        boxCornersW[i] = boxCenter + boxBasis * (kCornerSign[i] * halfExtent);
    }
    glm::vec3 qMin(std::numeric_limits<float>::max());
    glm::vec3 qMax(std::numeric_limits<float>::lowest());
    for (const auto& wc : boxCornersW) {
        const glm::vec3 lc = glm::vec3(invM * glm::vec4(wc, 1.0f));
        qMin = glm::min(qMin, lc);
        qMax = glm::max(qMax, lc);
    }
    std::vector<uint32_t> stack;
    stack.push_back(static_cast<uint32_t>(mesh.root));
    const glm::vec3 eps(1e-4f);
    while (!stack.empty()) {
        const uint32_t ni = stack.back();
        stack.pop_back();
        if (static_cast<int32_t>(ni) < 0 || ni >= mesh.nodes.size()) continue;
        const TriMeshBVHNode& node = mesh.nodes[ni];
        if (node.bmax.x < qMin.x || node.bmin.x > qMax.x ||
            node.bmax.y < qMin.y || node.bmin.y > qMax.y ||
            node.bmax.z < qMin.z || node.bmin.z > qMax.z) {
            continue;
        }
        if (node.count > 0) {
            for (uint32_t k = 0; k < node.count; ++k) {
                const uint32_t tri = mesh.triOrder[node.start + k];
                for (int v = 0; v < 3; ++v) {
                    const glm::vec3& tv = mesh.verts[mesh.indices[tri * 3u + static_cast<uint32_t>(v)]];
                    if (tv.x < qMin.x || tv.x > qMax.x || tv.y < qMin.y || tv.y > qMax.y ||
                        tv.z < qMin.z || tv.z > qMax.z) {
                        continue;
                    }
                    // Vertex inside query box: express in box frame, find min face clearance.
                    const glm::vec3 world = glm::vec3(meshProxy.meshMatrix * glm::vec4(tv, 1.0f));
                    const glm::vec3 bl = glm::vec3(invBox * glm::vec4(world, 1.0f));
                    const glm::vec3 dd = halfExtent - glm::abs(bl);
                    if (dd.x < -eps.x || dd.y < -eps.x || dd.z < -eps.x) continue;
                    const float m = std::min({dd.x, dd.y, dd.z});
                    glm::vec3 nLocal(0.0f);
                    if (m == dd.x) nLocal.x = (bl.x < 0.0f ? -1.0f : 1.0f);
                    else if (m == dd.y) nLocal.y = (bl.y < 0.0f ? -1.0f : 1.0f);
                    else nLocal.z = (bl.z < 0.0f ? -1.0f : 1.0f);
                    const glm::vec3 nWorld = glm::normalize(boxBasis * nLocal);
                    const float pen = m + cornerR;
                    if (pen > bestPen) {
                        bestPen = pen;
                        best.hit = true;
                        best.normal = nWorld; // mesh -> box direction approx via box face
                        best.penetration = pen;
                    }
                }
            }
        } else {
            if (node.left >= 0) stack.push_back(static_cast<uint32_t>(node.left));
            if (node.right >= 0) stack.push_back(static_cast<uint32_t>(node.right));
        }
    }
    return best;
}

// Mesh-involved dispatch. Convention (as elsewhere): normal points b -> a.
// NOTE: makeProxy() already converts fallback/dynamic meshes to Kind::Box,
// so Kind::Mesh here always means exact triangle data (triMesh != null).
Contact collideMesh(const Proxy& a, const Proxy& b) {
    const bool aExact = a.kind == Collider::Kind::Mesh && a.triMesh != nullptr;
    const bool bExact = b.kind == Collider::Kind::Mesh && b.triMesh != nullptr;
    // Exact mesh vs primitive.
    if (aExact && !bExact) {
        // b is primitive (or box-fallback mesh used as OBB below).
        if (b.kind == Collider::Kind::Mesh) {
            return boxBoxContact(a.center, a.rot, a.halfExtent, b.center, b.rot, b.halfExtent);
        }
        Contact c;
        if (b.kind == Collider::Kind::Sphere) {
            c = sphereTriMeshContact(b.center, b.radius, a);
        } else if (b.kind == Collider::Kind::Capsule) {
            c = capsuleTriMeshContact(b.center, b.rot, b.radius, b.halfHeight, a);
        } else {
            c = boxTriMeshContact(b.center, b.rot, b.halfExtent, a);
        }
        if (c.hit) c.normal = -c.normal; // mesh->prim becomes prim->mesh... see below
        // NOTE: narrowphase returns mesh->prim; here prim==a so negate to b->a.
        return c;
    }
    if (bExact && !aExact) {
        if (a.kind == Collider::Kind::Mesh) {
            return boxBoxContact(a.center, a.rot, a.halfExtent, b.center, b.rot, b.halfExtent);
        }
        // Narrowphase already returns mesh->prim, and here mesh==b, prim==a,
        // so the normal already points b -> a. No negation (negating here
        // pushed dynamic bodies INTO the mesh and caused tunneling).
        if (a.kind == Collider::Kind::Sphere) {
            return sphereTriMeshContact(a.center, a.radius, b);
        } else if (a.kind == Collider::Kind::Capsule) {
            return capsuleTriMeshContact(a.center, a.rot, a.radius, a.halfHeight, b);
        } else {
            return boxTriMeshContact(a.center, a.rot, a.halfExtent, b);
        }
    }
    // Both meshes (or both fallbacks): OBB approximation.
    return boxBoxContact(a.center, a.rot, a.halfExtent, b.center, b.rot, b.halfExtent);
}

Contact collide(const Proxy& a, const Proxy& b) {
    if (a.kind == Collider::Kind::Mesh || b.kind == Collider::Kind::Mesh) {
        return collideMesh(a, b);
    }
    switch (a.kind) {
    case Collider::Kind::Sphere:
        switch (b.kind) {
        case Collider::Kind::Sphere:
            return sphereSphereContact(a.center, a.radius, b.center, b.radius);
        case Collider::Kind::Box:
            return sphereBoxContact(a.center, a.radius, b.center, b.rot, b.halfExtent);
        case Collider::Kind::Capsule:
            return capsuleSphereContact(b.center, b.rot, b.radius, b.halfHeight, a.center, a.radius);
        }
        break;
    case Collider::Kind::Box:
        switch (b.kind) {
        case Collider::Kind::Sphere: {
            Contact c = sphereBoxContact(b.center, b.radius, a.center, a.rot, a.halfExtent);
            if (c.hit) c.normal = -c.normal;
            return c;
        }
        case Collider::Kind::Box:
            return boxBoxContact(a.center, a.rot, a.halfExtent, b.center, b.rot, b.halfExtent);
        case Collider::Kind::Capsule:
            return capsuleBoxContact(b.center, b.rot, b.radius, b.halfHeight, a.center, a.rot, a.halfExtent);
        }
        break;
    case Collider::Kind::Capsule:
        switch (b.kind) {
        case Collider::Kind::Sphere:
            return capsuleSphereContact(a.center, a.rot, a.radius, a.halfHeight, b.center, b.radius);
        case Collider::Kind::Box:
            return capsuleBoxContact(a.center, a.rot, a.radius, a.halfHeight, b.center, b.rot, b.halfExtent);
        case Collider::Kind::Capsule: {
            // Capsule-capsule: approximate via closest points of the segments.
            Contact c;
            glm::vec3 a0, a1, b0, b1;
            capsuleSegment(a.center, a.rot, a.halfHeight, a0, a1);
            capsuleSegment(b.center, b.rot, b.halfHeight, b0, b1);
            // Closest point between two segments (simplified: sample).
            glm::vec3 bestP = a0, bestQ = b0;
            float bestDist = std::numeric_limits<float>::max();
            const glm::vec3 samplesA[] = {a0, a1, (a0 + a1) * 0.5f};
            const glm::vec3 samplesB[] = {b0, b1, (b0 + b1) * 0.5f};
            for (const glm::vec3& pa : samplesA) {
                for (const glm::vec3& pb : samplesB) {
                    const float d = glm::length(pa - pb);
                    if (d < bestDist) {
                        bestDist = d;
                        bestP = pa;
                        bestQ = pb;
                    }
                }
            }
            const float r = a.radius + b.radius;
            if (bestDist >= r) {
                return c;
            }
            c.hit = true;
            const glm::vec3 delta = bestP - bestQ;
            if (bestDist > 1e-6f) {
                c.normal = delta / bestDist;
                c.penetration = r - bestDist;
            } else {
                c.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                c.penetration = r;
            }
            return c;
        }
        }
        break;
    }
    return Contact();
}

PhysicsMotionType motionTypeFor(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<ECS::RigidBodyComponent>(entity)) {
        return registry.get<ECS::RigidBodyComponent>(entity).motionType;
    }
    return PhysicsMotionType::Static;
}

} // namespace

void PhysicsSystem::step(Scene* scene, float deltaTime) {
    if (!m_Impl || !m_Impl->initialized || !scene || deltaTime <= 0.0f) {
        return;
    }

    deltaTime = std::min(deltaTime, kMaxDeltaTime);
    auto& registry = scene->getRegistry();

    // --- Kinematic bodies follow their transform (set by scripts / editor). ---
    for (const auto& [entity, state] : m_Impl->bodies) {
        if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) {
            continue;
        }
        if (motionTypeFor(registry, entity) == PhysicsMotionType::Kinematic) {
            // Kinematic bodies are driven by their transform; no integration.
            (void)state;
        }
    }

    // --- Integrate dynamic bodies. ---
    for (auto& [entity, state] : m_Impl->bodies) {
        if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) {
            continue;
        }
        if (motionTypeFor(registry, entity) != PhysicsMotionType::Dynamic) {
            continue;
        }

        Transform& t = registry.get<Transform>(entity);
        const ECS::RigidBodyComponent* rb = registry.all_of<ECS::RigidBodyComponent>(entity)
                                                ? &registry.get<ECS::RigidBodyComponent>(entity)
                                                : nullptr;
        const float gravityScale = rb ? rb->gravityScale : 1.0f;
        const float linearDamping = rb ? rb->linearDamping : 0.05f;
        const float angularDamping = rb ? rb->angularDamping : 0.05f;

        // Sleep check.
        const float linSpeed = glm::length(state.velocity);
        const float angSpeed = glm::length(state.angularVelocity);
        if (state.sleeping) {
            // Wake on external transform edits? Bodies are rebuilt on edits.
            continue;
        }
        if (linSpeed < kSleepLinearThreshold && angSpeed < kSleepAngularThreshold) {
            state.sleepTimer += deltaTime;
            if (state.sleepTimer >= kSleepTime) {
                state.sleeping = true;
                state.velocity = glm::vec3(0.0f);
                state.angularVelocity = glm::vec3(0.0f);
                continue;
            }
        } else {
            state.sleepTimer = 0.0f;
        }

        // Semi-implicit Euler.
        state.velocity += kGravity * gravityScale * deltaTime;
        state.velocity *= std::max(0.0f, 1.0f - linearDamping * deltaTime);
        state.angularVelocity *= std::max(0.0f, 1.0f - angularDamping * deltaTime);

        t.position += state.velocity * deltaTime;
        if (glm::dot(state.angularVelocity, state.angularVelocity) > 1e-8f) {
            const glm::quat dq = glm::quat(1.0f, state.angularVelocity * 0.5f * deltaTime);
            const glm::quat newRot = glm::normalize(rotationQuat(t) * dq);
            t.rotation = glm::degrees(glm::eulerAngles(newRot));
        }
    }

    // --- Collision detection + resolution. ---
    std::vector<entt::entity> dynamicBodies;
    std::vector<entt::entity> staticBodies;
    for (const auto& [entity, _] : m_Impl->bodies) {
        if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) {
            continue;
        }
        if (motionTypeFor(registry, entity) == PhysicsMotionType::Dynamic) {
            dynamicBodies.push_back(entity);
        } else {
            staticBodies.push_back(entity);
        }
    }

    // Dynamic vs dynamic (only one resolution per pair).
    for (size_t i = 0; i < dynamicBodies.size(); ++i) {
        const entt::entity a = dynamicBodies[i];
        for (size_t j = i + 1; j < dynamicBodies.size(); ++j) {
            const entt::entity b = dynamicBodies[j];
            const auto itA = m_Impl->colliders.find(a);
            const auto itB = m_Impl->colliders.find(b);
            if (itA == m_Impl->colliders.end() || itB == m_Impl->colliders.end()) {
                continue;
            }
            const Transform& ta = registry.get<Transform>(a);
            const Transform& tb = registry.get<Transform>(b);
            Proxy pa = makeProxy(ta, itA->second);
            Proxy pb = makeProxy(tb, itB->second);
            Contact contact = collide(pa, pb);
            if (!contact.hit || itA->second.isTrigger || itB->second.isTrigger) {
                continue;
            }
            // Resolve.
            const float invMassA = 1.0f;
            const float invMassB = 1.0f;
            const float invSum = invMassA + invMassB;
            const float penA = contact.penetration * (invMassA / invSum);
            const float penB = contact.penetration * (invMassB / invSum);

            Transform& taMut = registry.get<Transform>(a);
            Transform& tbMut = registry.get<Transform>(b);
            taMut.position += contact.normal * penA;
            tbMut.position -= contact.normal * penB;

            BodyState& stateA = m_Impl->bodies.at(a);
            BodyState& stateB = m_Impl->bodies.at(b);

            const glm::vec3 relVel = stateA.velocity - stateB.velocity;
            const float velAlongNormal = glm::dot(relVel, contact.normal);
            if (velAlongNormal < 0.0f) {
                const float restitution = 0.1f;
                const float j = -(1.0f + restitution) * velAlongNormal / invSum;
                stateA.velocity += contact.normal * j * invMassA;
                stateB.velocity -= contact.normal * j * invMassB;
                // Simplified friction: damp tangential velocity.
                const glm::vec3 tangent = glm::normalize(relVel - contact.normal * velAlongNormal);
                const float friction = 0.4f;
                stateA.velocity -= tangent * friction * std::abs(velAlongNormal);
            }
        }
    }

    // Dynamic vs static.
    for (entt::entity dyn : dynamicBodies) {
        auto itDyn = m_Impl->colliders.find(dyn);
        if (itDyn == m_Impl->colliders.end()) {
            continue;
        }
        BodyState& stateDyn = m_Impl->bodies.at(dyn);
        Transform& tDyn = registry.get<Transform>(dyn);

        for (entt::entity stat : staticBodies) {
            auto itStat = m_Impl->colliders.find(stat);
            if (itStat == m_Impl->colliders.end()) {
                continue;
            }
            const Transform& tStat = registry.get<Transform>(stat);
            Proxy pa = makeProxy(tDyn, itDyn->second);
            Proxy pb = makeProxy(tStat, itStat->second);
            Contact contact = collide(pa, pb);
            if (!contact.hit || itDyn->second.isTrigger || itStat->second.isTrigger) {
                continue;
            }
            tDyn.position += contact.normal * contact.penetration;
            const float velAlongNormal = glm::dot(stateDyn.velocity, contact.normal);
            if (velAlongNormal < 0.0f) {
                const ECS::RigidBodyComponent* rb =
                    registry.all_of<ECS::RigidBodyComponent>(dyn)
                        ? &registry.get<ECS::RigidBodyComponent>(dyn)
                        : nullptr;
                const float restitution = rb ? rb->restitution : 0.1f;
                const float friction = rb ? rb->friction : 0.4f;
                stateDyn.velocity -= contact.normal * (1.0f + restitution) * velAlongNormal;
                // Friction on the tangential component.
                const glm::vec3 tangential = stateDyn.velocity - contact.normal * glm::dot(stateDyn.velocity, contact.normal);
                const glm::vec3 frictionForce = -tangential * std::min(1.0f, friction);
                stateDyn.velocity += frictionForce;
            }
        }
    }

    // --- Write dynamic results back (already done inline), wake check. ---
    for (auto& [entity, state] : m_Impl->bodies) {
        if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) {
            continue;
        }
        if (motionTypeFor(registry, entity) == PhysicsMotionType::Dynamic) {
            if (glm::dot(state.velocity, state.velocity) > 0.01f) {
                state.sleepTimer = 0.0f;
                state.sleeping = false;
            }
        }
    }
}

bool PhysicsSystem::isInitialized() const {
    return m_Impl && m_Impl->initialized;
}

} // namespace Atlas::Physics
