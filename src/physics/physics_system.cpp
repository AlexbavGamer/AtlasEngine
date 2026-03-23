#include "physics_system.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/gtc/quaternion.hpp>

#include "../ecs/ecs.h"
#include "../ecs/components/components.h"
#include "../scene/scene.h"

namespace Atlas::Physics {
namespace {

using namespace JPH;

namespace Layers {
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING = 1;
    static constexpr ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS = 2;
}

class BroadPhaseLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BroadPhaseLayerInterfaceImpl() {
        m_ObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        m_ObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }

    uint GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        return m_ObjectToBroadPhase[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override {
        switch ((uint)layer) {
        case 0: return "NON_MOVING";
        case 1: return "MOVING";
        default: return "UNKNOWN";
        }
    }
#endif

private:
    BroadPhaseLayer m_ObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer layer1, BroadPhaseLayer layer2) const override {
        switch (layer1) {
        case Layers::NON_MOVING:
            return layer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            return false;
        }
    }
};

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer layer1, ObjectLayer layer2) const override {
        if (layer1 == Layers::NON_MOVING && layer2 == Layers::NON_MOVING) {
            return false;
        }
        return true;
    }
};

static void TraceImpl(const char* inFMT, ...) {
    va_list list;
    va_start(list, inFMT);
    vprintf(inFMT, list);
    va_end(list);
}

static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint inLine) {
    fprintf(stderr, "Jolt assert failed: %s | %s | %s:%u\n", inExpression, inMessage ? inMessage : "", inFile, inLine);
    return true;
}

static JPH::Vec3 toJolt(const glm::vec3& v) {
    return JPH::Vec3(v.x, v.y, v.z);
}

static glm::vec3 toGlm(const JPH::Vec3& v) {
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

static glm::quat toGlm(const JPH::Quat& q) {
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

static JPH::Quat toJolt(const glm::quat& q) {
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

static glm::quat transformRotationQuat(const Transform& t) {
    return glm::quat(glm::radians(t.rotation));
}

static PhysicsMotionType getMotionTypeForEntity(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<ECS::RigidBodyComponent>(entity)) {
        return registry.get<ECS::RigidBodyComponent>(entity).motionType;
    }
    return PhysicsMotionType::Static;
}

static JPH::EMotionType toJoltMotionType(PhysicsMotionType type) {
    switch (type) {
    case PhysicsMotionType::Dynamic: return JPH::EMotionType::Dynamic;
    case PhysicsMotionType::Kinematic: return JPH::EMotionType::Kinematic;
    case PhysicsMotionType::Static:
    default: return JPH::EMotionType::Static;
    }
}

static bool hasCollider(entt::registry& registry, entt::entity entity) {
    return registry.all_of<ECS::BoxColliderComponent>(entity) ||
           registry.all_of<ECS::SphereColliderComponent>(entity) ||
           registry.all_of<ECS::CapsuleColliderComponent>(entity);
}

static glm::vec3 rotatedOffset(const Transform& t, const glm::vec3& offset) {
    return glm::mat3_cast(transformRotationQuat(t)) * offset;
}

} // namespace

struct PhysicsSystem::Impl {
    bool initialized = false;
    Scene* scene = nullptr;

    std::unique_ptr<JPH::Factory> factory;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem;

    BroadPhaseLayerInterfaceImpl broadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
    ObjectLayerPairFilterImpl objectLayerPairFilter;

    std::unordered_map<uint32_t, JPH::BodyID> bodyIds;
};

PhysicsSystem::PhysicsSystem() : m_Impl(std::make_unique<Impl>()) {}

PhysicsSystem::~PhysicsSystem() {
    shutdown();
}

bool PhysicsSystem::initialize() {
    if (m_Impl->initialized) {
        return true;
    }

    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    m_Impl->factory = std::make_unique<JPH::Factory>();
    JPH::Factory::sInstance = m_Impl->factory.get();
    JPH::RegisterTypes();

    constexpr uint cMaxBodies = 8192;
    constexpr uint cNumBodyMutexes = 0;
    constexpr uint cMaxBodyPairs = 8192;
    constexpr uint cMaxContactConstraints = 8192;

    m_Impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(8 * 1024 * 1024);
    const uint workerThreads = std::max(1u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u);
    m_Impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerThreads);
    m_Impl->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_Impl->physicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
        m_Impl->broadPhaseLayerInterface, m_Impl->objectVsBroadPhaseLayerFilter, m_Impl->objectLayerPairFilter);
    m_Impl->physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    m_Impl->initialized = true;
    return true;
}

void PhysicsSystem::shutdown() {
    if (!m_Impl || !m_Impl->initialized) {
        return;
    }

    clear();
    m_Impl->physicsSystem.reset();
    m_Impl->jobSystem.reset();
    m_Impl->tempAllocator.reset();

    JPH::UnregisterTypes();
    JPH::Factory::sInstance = nullptr;
    m_Impl->factory.reset();

    m_Impl->initialized = false;
}

void PhysicsSystem::clear() {
    if (!m_Impl || !m_Impl->initialized || !m_Impl->physicsSystem) {
        if (m_Impl) {
            m_Impl->bodyIds.clear();
            m_Impl->scene = nullptr;
        }
        return;
    }

    auto& bodyInterface = m_Impl->physicsSystem->GetBodyInterface();
    std::vector<JPH::BodyID> ids;
    ids.reserve(m_Impl->bodyIds.size());
    for (const auto& [_, bodyId] : m_Impl->bodyIds) {
        ids.push_back(bodyId);
    }

    for (const JPH::BodyID& bodyId : ids) {
        if (bodyId.IsInvalid()) {
            continue;
        }
        if (bodyInterface.IsAdded(bodyId)) {
            bodyInterface.RemoveBody(bodyId);
        }
        bodyInterface.DestroyBody(bodyId);
    }

    m_Impl->bodyIds.clear();
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
    auto& bodyInterface = m_Impl->physicsSystem->GetBodyInterface();

    for (entt::entity entity : scene->getAllEntities()) {
        if (!registry.valid(entity) || !registry.all_of<Transform>(entity) || !hasCollider(registry, entity)) {
            continue;
        }
        if (registry.all_of<ECS::EditorHiddenComponent>(entity) && registry.get<ECS::EditorHiddenComponent>(entity).hidden) {
            continue;
        }

        const Transform& t = registry.get<Transform>(entity);
        const glm::quat rot = transformRotationQuat(t);

        JPH::RefConst<JPH::Shape> shape;
        glm::vec3 offset(0.0f);

        if (registry.all_of<ECS::BoxColliderComponent>(entity)) {
            const auto& c = registry.get<ECS::BoxColliderComponent>(entity);
            shape = new JPH::BoxShape(toJolt(glm::max(c.halfExtent, glm::vec3(0.01f))));
            offset = c.offset;
        } else if (registry.all_of<ECS::SphereColliderComponent>(entity)) {
            const auto& c = registry.get<ECS::SphereColliderComponent>(entity);
            shape = new JPH::SphereShape(std::max(0.01f, c.radius));
            offset = c.offset;
        } else if (registry.all_of<ECS::CapsuleColliderComponent>(entity)) {
            const auto& c = registry.get<ECS::CapsuleColliderComponent>(entity);
            shape = new JPH::CapsuleShape(std::max(0.01f, c.halfHeight), std::max(0.01f, c.radius));
            offset = c.offset;
        }

        if (shape == nullptr) {
            continue;
        }

        const glm::vec3 pos = t.position + rotatedOffset(t, offset);
        const PhysicsMotionType motionType = getMotionTypeForEntity(registry, entity);
        const JPH::EMotionType joltMotion = toJoltMotionType(motionType);
        const JPH::ObjectLayer layer = (joltMotion == JPH::EMotionType::Static) ? Layers::NON_MOVING : Layers::MOVING;

        JPH::BodyCreationSettings settings(shape, JPH::RVec3(pos.x, pos.y, pos.z), toJolt(rot), joltMotion, layer);
        settings.mUserData = static_cast<uint64_t>(entt::to_integral(entity));
        settings.mAllowSleeping = true;

        if (registry.all_of<ECS::RigidBodyComponent>(entity)) {
            const auto& rb = registry.get<ECS::RigidBodyComponent>(entity);
            settings.mFriction = rb.friction;
            settings.mRestitution = rb.restitution;
            settings.mLinearDamping = rb.linearDamping;
            settings.mAngularDamping = rb.angularDamping;
            settings.mGravityFactor = rb.gravityScale;
            settings.mAllowSleeping = rb.allowSleep;
            settings.mMotionQuality = rb.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        }

        const JPH::EActivation activation = (joltMotion == JPH::EMotionType::Static) ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, activation);
        if (bodyId.IsInvalid()) {
            continue;
        }

        m_Impl->bodyIds.emplace(static_cast<uint32_t>(entt::to_integral(entity)), bodyId);
    }

    m_Impl->physicsSystem->OptimizeBroadPhase();
}

void PhysicsSystem::step(Scene* scene, float deltaTime) {
    if (!m_Impl || !m_Impl->initialized || !m_Impl->physicsSystem || !scene || deltaTime <= 0.0f) {
        return;
    }

    auto& registry = scene->getRegistry();
    auto& bodyInterface = m_Impl->physicsSystem->GetBodyInterface();

    for (const auto& [entityId, bodyId] : m_Impl->bodyIds) {
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) {
            continue;
        }
        if (!bodyInterface.IsAdded(bodyId)) {
            continue;
        }

        if (getMotionTypeForEntity(registry, entity) == PhysicsMotionType::Kinematic) {
            const Transform& t = registry.get<Transform>(entity);
            const glm::quat rot = transformRotationQuat(t);
            glm::vec3 offset(0.0f);
            if (registry.all_of<ECS::BoxColliderComponent>(entity)) offset = registry.get<ECS::BoxColliderComponent>(entity).offset;
            else if (registry.all_of<ECS::SphereColliderComponent>(entity)) offset = registry.get<ECS::SphereColliderComponent>(entity).offset;
            else if (registry.all_of<ECS::CapsuleColliderComponent>(entity)) offset = registry.get<ECS::CapsuleColliderComponent>(entity).offset;
            const glm::vec3 pos = t.position + rotatedOffset(t, offset);
            bodyInterface.MoveKinematic(bodyId, JPH::RVec3(pos.x, pos.y, pos.z), toJolt(rot), deltaTime);
        }
    }

    m_Impl->physicsSystem->Update(deltaTime, 1, m_Impl->tempAllocator.get(), m_Impl->jobSystem.get());

    for (const auto& [entityId, bodyId] : m_Impl->bodyIds) {
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) {
            continue;
        }
        if (!bodyInterface.IsAdded(bodyId)) {
            continue;
        }

        const PhysicsMotionType motionType = getMotionTypeForEntity(registry, entity);
        if (motionType == PhysicsMotionType::Static) {
            continue;
        }

        Transform& t = registry.get<Transform>(entity);
        const JPH::RVec3 bodyPos = bodyInterface.GetPosition(bodyId);
        const JPH::Quat bodyRot = bodyInterface.GetRotation(bodyId);
        const glm::quat rot = toGlm(bodyRot);

        glm::vec3 offset(0.0f);
        if (registry.all_of<ECS::BoxColliderComponent>(entity)) offset = registry.get<ECS::BoxColliderComponent>(entity).offset;
        else if (registry.all_of<ECS::SphereColliderComponent>(entity)) offset = registry.get<ECS::SphereColliderComponent>(entity).offset;
        else if (registry.all_of<ECS::CapsuleColliderComponent>(entity)) offset = registry.get<ECS::CapsuleColliderComponent>(entity).offset;

        t.position = glm::vec3(static_cast<float>(bodyPos.GetX()), static_cast<float>(bodyPos.GetY()), static_cast<float>(bodyPos.GetZ())) - (glm::mat3_cast(rot) * offset);
        t.rotation = glm::degrees(glm::eulerAngles(rot));
    }
}

bool PhysicsSystem::isInitialized() const {
    return m_Impl && m_Impl->initialized;
}

} // namespace Atlas::Physics
