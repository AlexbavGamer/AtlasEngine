#pragma once

#include <cstdint>
#include <vector>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "ui_types.h"

namespace Atlas::UI {

// A run of draw-list primitives sharing the same texture + scissor rect. The
// renderer issues one vkCmdDrawIndexed per batch.
struct UIBatch {
    uint32_t texture = 0;    // index into the renderer's texture array
    glm::vec4 scissor{0.0f}; // x, y, w, h in screen px; w/h == 0 -> no clip
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};

// Batched quad/text/image list. Widgets push primitives during a frame; the
// renderer uploads the whole buffer once at the end of the frame.
class UIDrawList {
public:
    void clear() {
        m_Vertices.clear();
        m_Indices.clear();
        m_Batches.clear();
        m_CurrentTexture = 0;
        m_CurrentScissor = glm::vec4(0.0f);
        m_BatchDirty = false;
    }

    // --- Batch state -----------------------------------------------------
    // Selecting a different texture or scissor closes the current batch; the
    // next primitive starts a new one.
    void setTexture(uint32_t idx) {
        if (idx == m_CurrentTexture) return;
        m_CurrentTexture = idx;
        m_BatchDirty = true;
    }
    uint32_t texture() const { return m_CurrentTexture; }

    // Force a batch boundary unconditionally (unlike setTexture/setScissor,
    // which only close when the value changes). Guarantees a widget drawn after
    // this starts a fresh batch with the current texture + scissor, so a prior
    // widget's scissor cannot leak into it.
    void breakBatch() { m_BatchDirty = true; }

    void setScissor(float x, float y, float w, float h) {
        const glm::vec4 s(x, y, w, h);
        if (s == m_CurrentScissor) return;
        m_CurrentScissor = s;
        m_BatchDirty = true;
    }
    void clearScissor() { setScissor(0.0f, 0.0f, 0.0f, 0.0f); }
    const glm::vec4& scissor() const { return m_CurrentScissor; }

    // --- Primitives ------------------------------------------------------
    void addQuad(const glm::vec2& min, const glm::vec2& max,
                 const glm::vec4& color,
                 const glm::vec2& uvMin = glm::vec2(kSolidU, kSolidV),
                 const glm::vec2& uvMax = glm::vec2(kSolidU, kSolidV)) {
        // The fragment shader multiplies the tint by the texture alpha. Solid
        // quads sample our opaque-white texel (alpha 1), so we premultiply the
        // tint here so semi-transparent fills (window/panel) blend correctly.
        glm::vec4 pc = color;
        pc.r *= pc.a; pc.g *= pc.a; pc.b *= pc.a;
        const uint32_t tex = m_CurrentTexture;
        beginBatch();
        const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
        m_Vertices.push_back({min, uvMin, pc, tex});
        m_Vertices.push_back({glm::vec2(max.x, min.y), glm::vec2(uvMax.x, uvMin.y), pc, tex});
        m_Vertices.push_back({max, uvMax, pc, tex});
        m_Vertices.push_back({glm::vec2(min.x, max.y), glm::vec2(uvMin.x, uvMax.y), pc, tex});
        m_Indices.push_back(base + 0);
        m_Indices.push_back(base + 1);
        m_Indices.push_back(base + 2);
        m_Indices.push_back(base + 0);
        m_Indices.push_back(base + 2);
        m_Indices.push_back(base + 3);
        endBatch(4, 6);
    }

    void addTriangle(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2,
                     const glm::vec4& color,
                     const glm::vec2& uv0 = glm::vec2(kSolidU, kSolidV),
                     const glm::vec2& uv1 = glm::vec2(kSolidU, kSolidV),
                     const glm::vec2& uv2 = glm::vec2(kSolidU, kSolidV)) {
        glm::vec4 pc = color;
        pc.r *= pc.a; pc.g *= pc.a; pc.b *= pc.a;
        const uint32_t tex = m_CurrentTexture;
        beginBatch();
        const uint32_t base = static_cast<uint32_t>(m_Vertices.size());
        m_Vertices.push_back({p0, uv0, pc, tex});
        m_Vertices.push_back({p1, uv1, pc, tex});
        m_Vertices.push_back({p2, uv2, pc, tex});
        m_Indices.push_back(base + 0);
        m_Indices.push_back(base + 1);
        m_Indices.push_back(base + 2);
        endBatch(3, 3);
    }

    // Rounded rectangle with smooth arc corners.
    void addRoundedRect(const glm::vec2& min, const glm::vec2& max,
                        float radius, const glm::vec4& color) {
        const float w = max.x - min.x;
        const float h = max.y - min.y;
        const float r = glm::clamp(radius, 0.0f, 0.5f * glm::min(w, h));

        // Center rect.
        addQuad(min + glm::vec2(r, r), max - glm::vec2(r, r), color);
        // Left / right strips.
        addQuad(min + glm::vec2(0.0f, r), glm::vec2(min.x + r, max.y - r), color);
        addQuad(glm::vec2(max.x - r, min.y + r), max - glm::vec2(0.0f, r), color);
        // Top / bottom strips.
        addQuad(glm::vec2(min.x + r, min.y), glm::vec2(max.x - r, min.y + r), color);
        addQuad(glm::vec2(min.x + r, max.y - r), glm::vec2(max.x - r, max.y), color);

        if (r <= 0.0f) {
            addQuad(min, max, color);
            return;
        }

        // Rounded corners via triangle fans.
        constexpr int kSegments = 8;
        const float kHalfPi = glm::half_pi<float>();
        auto addCorner = [&](const glm::vec2& arcCenter, float startAngle) {
            for (int i = 0; i < kSegments; ++i) {
                const float a0 = startAngle + (static_cast<float>(i) / float(kSegments)) * kHalfPi;
                const float a1 = startAngle + (static_cast<float>(i + 1) / float(kSegments)) * kHalfPi;
                const glm::vec2 p0 = arcCenter + glm::vec2(std::cos(a0), std::sin(a0)) * r;
                const glm::vec2 p1 = arcCenter + glm::vec2(std::cos(a1), std::sin(a1)) * r;
                addTriangle(arcCenter, p0, p1, color);
            }
        };

        // TL: pi -> 3pi/2 (left -> top), TR: 3pi/2 -> 2pi (top -> right),
        // BR: 0 -> pi/2 (right -> bottom), BL: pi/2 -> pi (bottom -> left).
        addCorner(glm::vec2(min.x + r, min.y + r), glm::pi<float>());
        addCorner(glm::vec2(max.x - r, min.y + r), glm::half_pi<float>() * 3.0f);
        addCorner(glm::vec2(max.x - r, max.y - r), 0.0f);
        addCorner(glm::vec2(min.x + r, max.y - r), glm::half_pi<float>());
    }

    // Textured quad (sprite / font glyph). Samples the current texture.
    void addImage(const glm::vec2& min, const glm::vec2& max,
                  const glm::vec2& uvMin, const glm::vec2& uvMax,
                  const glm::vec4& tint) {
        addQuad(min, max, tint, uvMin, uvMax);
    }

    const std::vector<UIVertex>& vertices() const { return m_Vertices; }
    const std::vector<uint32_t>& indices() const { return m_Indices; }
    const std::vector<UIBatch>& batches() const { return m_Batches; }
    bool empty() const { return m_Vertices.empty(); }

private:
    void beginBatch() {
        if (m_BatchDirty || m_Batches.empty()) {
            UIBatch b;
            b.texture = m_CurrentTexture;
            b.scissor = m_CurrentScissor;
            b.vertexOffset = static_cast<uint32_t>(m_Vertices.size());
            b.indexOffset = static_cast<uint32_t>(m_Indices.size());
            m_Batches.push_back(b);
            m_BatchDirty = false;
        }
    }

    void endBatch(uint32_t vertCount, uint32_t idxCount) {
        if (m_Batches.empty()) return;
        auto& b = m_Batches.back();
        b.vertexCount += vertCount;
        b.indexCount += idxCount;
    }

    std::vector<UIVertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<UIBatch> m_Batches;
    uint32_t m_CurrentTexture = 0;
    glm::vec4 m_CurrentScissor{0.0f};
    bool m_BatchDirty = false;
};

} // namespace Atlas::UI
