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

struct Collider {
    enum class Kind : uint8_t { Box, Sphere, Capsule };
    Kind kind = Kind::Sphere;
    glm::vec3 halfExtent = glm::vec3(0.5f); // box
    float radius = 0.5f;                    // sphere / capsule
    float halfHeight = 0.5f;                // capsule
    glm::vec3 offset = glm::vec3(0.0f);
    bool isTrigger = false;
};

Collider colliderFromEntity(entt::registry& registry, entt::entity entity) {
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

        Collider collider = colliderFromEntity(registry, entity);
        const bool hasCollider =
            registry.all_of<ECS::BoxColliderComponent>(entity) ||
            registry.all_of<ECS::SphereColliderComponent>(entity) ||
            registry.all_of<ECS::CapsuleColliderComponent>(entity);
        if (!hasCollider) {
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
};

Proxy makeProxy(const Transform& t, const Collider& c) {
    Proxy p;
    p.kind = c.kind;
    p.center = t.position + rotatedOffset(t, c.offset);
    p.rot = rotationQuat(t);
    p.halfExtent = c.halfExtent;
    p.radius = c.radius;
    p.halfHeight = c.halfHeight;
    return p;
}

Contact collide(const Proxy& a, const Proxy& b) {
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
