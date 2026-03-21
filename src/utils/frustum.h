#pragma once

#include <glm/glm.hpp>

namespace Atlas {

struct FrustumPlane {
    glm::vec3 n{0.0f};
    float d = 0.0f;

    float distance(const glm::vec3& p) const {
        return glm::dot(n, p) + d;
    }
};

struct Frustum {
    // Planes are oriented so that points inside satisfy plane.distance(p) >= -radius.
    FrustumPlane planes[6];

    static Frustum fromViewProj(const glm::mat4& m) {
        // m is expected to be projection * view (same convention used for rendering).
        Frustum f;

        // Extract planes from clip matrix.
        // Row-major extraction using glm's column-major storage: row i is (m[0][i], m[1][i], m[2][i], m[3][i]).
        const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
        const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
        const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
        const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

        glm::vec4 p;

        // Left
        p = row3 + row0;
        f.planes[0].n = glm::vec3(p);
        f.planes[0].d = p.w;

        // Right
        p = row3 - row0;
        f.planes[1].n = glm::vec3(p);
        f.planes[1].d = p.w;

        // Bottom
        p = row3 + row1;
        f.planes[2].n = glm::vec3(p);
        f.planes[2].d = p.w;

        // Top
        p = row3 - row1;
        f.planes[3].n = glm::vec3(p);
        f.planes[3].d = p.w;

        // Near
        p = row3 + row2;
        f.planes[4].n = glm::vec3(p);
        f.planes[4].d = p.w;

        // Far
        p = row3 - row2;
        f.planes[5].n = glm::vec3(p);
        f.planes[5].d = p.w;

        // Normalize planes.
        for (auto& plane : f.planes) {
            float len = glm::length(plane.n);
            if (len > 0.0f) {
                plane.n /= len;
                plane.d /= len;
            }
        }

        return f;
    }

    bool testSphere(const glm::vec3& center, float radius) const {
        for (const auto& plane : planes) {
            if (plane.distance(center) < -radius) {
                return false;
            }
        }
        return true;
    }
};

} // namespace Atlas
