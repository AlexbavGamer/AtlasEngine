#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Atlas {
namespace Anim {

struct TRS {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

inline glm::mat4 toMat4(const TRS& trs) {
    glm::mat4 m(1.0f);
    m = glm::translate(m, trs.translation);
    m *= glm::mat4_cast(trs.rotation);
    m = glm::scale(m, trs.scale);
    return m;
}

struct PoseOverrides {
    const std::vector<uint8_t>* hasRotation = nullptr;
    const std::vector<glm::quat>* rotation = nullptr;
};

inline bool overrideRotation(const PoseOverrides& o, uint32_t i, glm::quat& outRot) {
    if (!o.hasRotation || !o.rotation) return false;
    if (i >= o.hasRotation->size() || i >= o.rotation->size()) return false;
    if ((*o.hasRotation)[i] == 0) return false;
    outRot = (*o.rotation)[i];
    return true;
}

template <typename T>
struct Key {
    float time = 0.0f;
    T value{};
};

struct BoneTrack {
    uint32_t boneIndex = 0;
    std::vector<Key<glm::vec3>> translationKeys;
    std::vector<Key<glm::quat>> rotationKeys;
    std::vector<Key<glm::vec3>> scaleKeys;
};

struct AnimationClip {
    std::string name;
    float durationSeconds = 0.0f;
    std::vector<BoneTrack> tracks;

    // boneIndex -> index into tracks
    std::unordered_map<uint32_t, size_t> boneToTrack;
};

struct Skeleton {
    std::vector<std::string> boneNames;
    std::vector<int32_t> parentIndex; // -1 for root
    std::vector<TRS> bindLocal;
    std::vector<glm::mat4> inverseBind;
    std::unordered_map<std::string, uint32_t> nameToIndex;

    uint32_t boneCount() const { return static_cast<uint32_t>(boneNames.size()); }
};

struct AnimationPlayer {
    int32_t clipIndex = -1;
    float timeSeconds = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool playing = true;
};

inline float wrapTime(float t, float duration) {
    if (duration <= 0.0f) return 0.0f;
    float out = std::fmod(t, duration);
    if (out < 0.0f) out += duration;
    return out;
}

inline size_t findKeyIndex(const std::vector<Key<glm::vec3>>& keys, float t) {
    if (keys.size() <= 1) return 0;
    auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float v, const Key<glm::vec3>& k) {
        return v < k.time;
    });
    if (it == keys.begin()) return 0;
    return static_cast<size_t>(std::distance(keys.begin(), it) - 1);
}

inline size_t findKeyIndexQuat(const std::vector<Key<glm::quat>>& keys, float t) {
    if (keys.size() <= 1) return 0;
    auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float v, const Key<glm::quat>& k) {
        return v < k.time;
    });
    if (it == keys.begin()) return 0;
    return static_cast<size_t>(std::distance(keys.begin(), it) - 1);
}

inline glm::vec3 sampleVec3(const std::vector<Key<glm::vec3>>& keys, float t, const glm::vec3& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0].value;

    size_t i0 = findKeyIndex(keys, t);
    size_t i1 = std::min(i0 + 1, keys.size() - 1);
    const auto& k0 = keys[i0];
    const auto& k1 = keys[i1];
    if (k0.time == k1.time) return k0.value;

    float a = (t - k0.time) / (k1.time - k0.time);
    a = std::clamp(a, 0.0f, 1.0f);
    return glm::mix(k0.value, k1.value, a);
}

inline glm::quat sampleQuat(const std::vector<Key<glm::quat>>& keys, float t, const glm::quat& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return glm::normalize(keys[0].value);

    size_t i0 = findKeyIndexQuat(keys, t);
    size_t i1 = std::min(i0 + 1, keys.size() - 1);
    const auto& k0 = keys[i0];
    const auto& k1 = keys[i1];
    if (k0.time == k1.time) return glm::normalize(k0.value);

    float a = (t - k0.time) / (k1.time - k0.time);
    a = std::clamp(a, 0.0f, 1.0f);
    return glm::normalize(glm::slerp(k0.value, k1.value, a));
}

inline void evaluateGlobals(const Skeleton& skel, const AnimationClip* clip, float tSeconds,
                           const PoseOverrides& overrides,
                           std::vector<glm::mat4>& outGlobals) {
    const uint32_t boneCount = skel.boneCount();
    outGlobals.resize(boneCount);

    float t = tSeconds;
    if (clip) {
        t = wrapTime(tSeconds, clip->durationSeconds);
    } else {
        t = 0.0f;
    }

    for (uint32_t i = 0; i < boneCount; ++i) {
        TRS localTRS = (i < skel.bindLocal.size()) ? skel.bindLocal[i] : TRS{};

        if (clip) {
            auto it = clip->boneToTrack.find(i);
            if (it != clip->boneToTrack.end()) {
                const BoneTrack& tr = clip->tracks[it->second];
                localTRS.translation = sampleVec3(tr.translationKeys, t, localTRS.translation);
                localTRS.rotation = sampleQuat(tr.rotationKeys, t, localTRS.rotation);
                localTRS.scale = sampleVec3(tr.scaleKeys, t, localTRS.scale);
            }
        }

        glm::quat rotOverride;
        if (overrideRotation(overrides, i, rotOverride)) {
            localTRS.rotation = rotOverride;
        }

        glm::mat4 localM = toMat4(localTRS);
        int32_t p = (i < skel.parentIndex.size()) ? skel.parentIndex[i] : -1;
        if (p >= 0 && static_cast<uint32_t>(p) < boneCount) {
            outGlobals[i] = outGlobals[static_cast<uint32_t>(p)] * localM;
        } else {
            outGlobals[i] = localM;
        }
    }
}

inline void evaluatePose(const Skeleton& skel, const AnimationClip* clip, float tSeconds,
                         std::vector<glm::mat4>& outFinalSkinMatrices) {
    PoseOverrides overrides;
    std::vector<glm::mat4> globals;
    evaluateGlobals(skel, clip, tSeconds, overrides, globals);

    const uint32_t boneCount = skel.boneCount();
    outFinalSkinMatrices.resize(boneCount);
    for (uint32_t i = 0; i < boneCount; ++i) {
        glm::mat4 invBind = (i < skel.inverseBind.size()) ? skel.inverseBind[i] : glm::mat4(1.0f);
        outFinalSkinMatrices[i] = globals[i] * invBind;
    }
}

inline void evaluatePose(const Skeleton& skel, const AnimationClip* clip, float tSeconds,
                         const PoseOverrides& overrides,
                         std::vector<glm::mat4>& outFinalSkinMatrices) {
    std::vector<glm::mat4> globals;
    evaluateGlobals(skel, clip, tSeconds, overrides, globals);

    const uint32_t boneCount = skel.boneCount();
    outFinalSkinMatrices.resize(boneCount);
    for (uint32_t i = 0; i < boneCount; ++i) {
        glm::mat4 invBind = (i < skel.inverseBind.size()) ? skel.inverseBind[i] : glm::mat4(1.0f);
        outFinalSkinMatrices[i] = globals[i] * invBind;
    }
}

} // namespace Anim
} // namespace Atlas
