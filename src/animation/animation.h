#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
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
    int32_t rootMotionBoneIndex = -1;

    uint32_t boneCount() const { return static_cast<uint32_t>(boneNames.size()); }
};

struct AnimationPlayer {
    int32_t clipIndex = -1;
    float timeSeconds = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool playing = true;
    bool enableRootMotion = true;
    bool rootMotionApplyRotation = false;
    bool rootMotionApplyY = false;
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

inline TRS sampleLocalTRS(const Skeleton& skel, const AnimationClip* clip, float tSeconds,
                          const PoseOverrides& overrides, uint32_t boneIndex) {
    TRS localTRS = (boneIndex < skel.bindLocal.size()) ? skel.bindLocal[boneIndex] : TRS{};

    float t = tSeconds;
    if (clip) {
        t = wrapTime(tSeconds, clip->durationSeconds);
        auto it = clip->boneToTrack.find(boneIndex);
        if (it != clip->boneToTrack.end()) {
            const BoneTrack& tr = clip->tracks[it->second];
            localTRS.translation = sampleVec3(tr.translationKeys, t, localTRS.translation);
            localTRS.rotation = sampleQuat(tr.rotationKeys, t, localTRS.rotation);
            localTRS.scale = sampleVec3(tr.scaleKeys, t, localTRS.scale);
        }
    }

    glm::quat rotOverride;
    if (overrideRotation(overrides, boneIndex, rotOverride)) {
        localTRS.rotation = rotOverride;
    }

    return localTRS;
}

inline int32_t chooseRootMotionBone(const Skeleton& skel, const std::vector<AnimationClip>& clips) {
    if (skel.boneCount() == 0 || clips.empty()) return -1;

    auto normalizeName = [](const std::string& name) {
        std::string lower;
        lower.reserve(name.size());
        for (unsigned char c : name) {
            if (std::isalnum(c)) {
                lower.push_back(static_cast<char>(std::tolower(c)));
            }
        }
        return lower;
    };

    auto depthOf = [&](uint32_t boneIndex) {
        int depth = 0;
        int32_t p = (boneIndex < skel.parentIndex.size()) ? skel.parentIndex[boneIndex] : -1;
        while (p >= 0 && depth < 128) {
            ++depth;
            p = (static_cast<size_t>(p) < skel.parentIndex.size()) ? skel.parentIndex[static_cast<size_t>(p)] : -1;
        }
        return depth;
    };

    auto motionMagnitudeOf = [&](uint32_t boneIndex, bool& hasTranslationTrack) {
        float motionMagnitude = 0.0f;
        hasTranslationTrack = false;

        for (const auto& clip : clips) {
            auto it = clip.boneToTrack.find(boneIndex);
            if (it == clip.boneToTrack.end()) continue;
            const BoneTrack& track = clip.tracks[it->second];
            if (track.translationKeys.empty()) continue;

            hasTranslationTrack = true;
            glm::vec3 minV = track.translationKeys.front().value;
            glm::vec3 maxV = minV;
            for (const auto& key : track.translationKeys) {
                minV = glm::min(minV, key.value);
                maxV = glm::max(maxV, key.value);
            }
            motionMagnitude = std::max(motionMagnitude, glm::length(maxV - minV));
        }

        return motionMagnitude;
    };

    auto nameScore = [&](const std::string& name) {
        const std::string lower = normalizeName(name);

        if (lower == "trans" || lower == "translation" || lower == "trajectory" || lower == "traj" ||
            lower == "rootmotion" || lower == "motion") return 220;
        if (lower == "root" || lower == "armature" || lower == "master" || lower == "boneroot") return 150;
        if (lower == "hip" || lower == "hips" || lower == "pelvis") return 100;
        if (lower.find("rootmotion") != std::string::npos) return 200;
        if (lower.find("trans") != std::string::npos || lower.find("trajectory") != std::string::npos) return 180;
        if (lower.find("root") != std::string::npos) return 120;
        if (lower.find("hips") != std::string::npos || lower.find("pelvis") != std::string::npos || lower.find("hip") != std::string::npos) return 80;
        if (lower.find("cog") != std::string::npos || lower.find("center") != std::string::npos) return 70;
        if (lower.find("ik") != std::string::npos || lower.find("fk") != std::string::npos || lower.find("pole") != std::string::npos ||
            lower.find("end") != std::string::npos || lower.find("cam") != std::string::npos || lower.find("weapon") != std::string::npos) return -80;
        return 0;
    };

    const std::array<const char*, 9> exactPreferred = {
        "trans", "translation", "trajectory", "traj", "rootmotion", "motion", "root", "boneroot", "master"
    };

    for (const char* preferred : exactPreferred) {
        for (uint32_t boneIndex = 0; boneIndex < skel.boneCount(); ++boneIndex) {
            const std::string normalized = normalizeName(boneIndex < skel.boneNames.size() ? skel.boneNames[boneIndex] : std::string());
            if (normalized != preferred) continue;

            bool hasTranslationTrack = false;
            motionMagnitudeOf(boneIndex, hasTranslationTrack);
            if (hasTranslationTrack) {
                return static_cast<int32_t>(boneIndex);
            }
        }
    }

    int32_t bestBone = -1;
    float bestScore = -1000000.0f;

    for (uint32_t boneIndex = 0; boneIndex < skel.boneCount(); ++boneIndex) {
        bool hasTranslationTrack = false;
        const float motionMagnitude = motionMagnitudeOf(boneIndex, hasTranslationTrack);
        if (!hasTranslationTrack) continue;

        const std::string boneName = boneIndex < skel.boneNames.size() ? skel.boneNames[boneIndex] : std::string();
        float score = static_cast<float>(nameScore(boneName));
        score += motionMagnitude * 10.0f;
        score -= static_cast<float>(depthOf(boneIndex)) * 2.0f;

        int32_t parent = (boneIndex < skel.parentIndex.size()) ? skel.parentIndex[boneIndex] : -1;
        if (parent >= 0 && static_cast<size_t>(parent) < skel.boneNames.size()) {
            score += static_cast<float>(nameScore(skel.boneNames[static_cast<size_t>(parent)])) * 0.15f;
        }

        if (score > bestScore) {
            bestScore = score;
            bestBone = static_cast<int32_t>(boneIndex);
        }
    }

    return bestBone;
}

inline void evaluateGlobals(const Skeleton& skel, const AnimationClip* clip, float tSeconds,
                           const PoseOverrides& overrides,
                           std::vector<glm::mat4>& outGlobals,
                           int32_t lockedBoneIndex = -1,
                           bool lockTranslation = false,
                           bool lockRotation = false) {
    const uint32_t boneCount = skel.boneCount();
    outGlobals.resize(boneCount);

    for (uint32_t i = 0; i < boneCount; ++i) {
        TRS localTRS = sampleLocalTRS(skel, clip, tSeconds, overrides, i);

        if (static_cast<int32_t>(i) == lockedBoneIndex) {
            const TRS bindTRS = (i < skel.bindLocal.size()) ? skel.bindLocal[i] : TRS{};
            if (lockTranslation) {
                localTRS.translation = bindTRS.translation;
            }
            if (lockRotation) {
                localTRS.rotation = bindTRS.rotation;
            }
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
