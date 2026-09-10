#pragma once

#include <array>
#include <vector>
#include <cstdlib>

#include <glm/glm.hpp>

#include "ui_types.h"
#include "ui_draw_list.h"

// stb_truetype header — define the implementation in exactly one TU (ui_font.cpp).
#ifdef ATLAS_UI_FONT_IMPL
#define STB_TRUETYPE_IMPLEMENTATION
#endif
#include <stb_truetype.h>

namespace Atlas::UI {

// Rasterized glyph atlas created from a TrueType font. The atlas is a single
// RGBA texture packing every glyph we need. Characters outside the atlas render
// as a fallback box.
class UIFont {
public:
    static constexpr int kAtlasW = 1024;
    static constexpr int kAtlasH = 1024;
    static constexpr int kFirstChar = 32;
    static constexpr int kNumChars = 224; // 32-255 (ASCII + Latin-1 supplement)
    static constexpr int kSolidSpan = 8;  // opaque-white block at bottom-right

    // Build the font atlas from a TTF file loaded into memory. The UIFont keeps
    // its own copy of the TTF data because stb_truetype stores a raw pointer
    // into it; the caller's buffer may go out of scope.
    bool load(const unsigned char* ttfData, size_t ttfSize, float fontSize = 16.0f) {
        m_TTFData.assign(ttfData, ttfData + ttfSize);
        if (!stbtt_InitFont(&m_Font, m_TTFData.data(), 0)) {
            m_TTFData.clear();
            return false;
        }

        m_FontSize = fontSize; // logical (design) size — drives all widget scaling
        // Bake glyphs at 2x the logical size so small text minifies from a
        // higher-res source and stays crisp instead of aliasing.
        const float bakeFontSize = fontSize * 2.0f;
        m_Scale = stbtt_ScaleForPixelHeight(&m_Font, fontSize);

        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(&m_Font, &ascent, &descent, &lineGap);
        m_Ascent = static_cast<float>(ascent) * m_Scale;
        m_LineHeight = static_cast<float>(ascent - descent + lineGap) * m_Scale;

        // Rasterize each glyph and pack into the atlas.
        std::vector<unsigned char> bitmap(kAtlasW * kAtlasH, 0);
        stbtt_pack_context pc;
        stbtt_PackBegin(&pc, bitmap.data(), kAtlasW, kAtlasH, 0, 1, nullptr);

        stbtt_packedchar packed[kNumChars];
        stbtt_PackFontRange(&pc, ttfData, 0, bakeFontSize, kFirstChar, kNumChars, packed);
        stbtt_PackEnd(&pc);

        // Convert the alpha-only bitmap to RGBA (white text, alpha from atlas).
        m_AtlasRGBA.resize(kAtlasW * kAtlasH * 4);
        for (int i = 0; i < kAtlasW * kAtlasH; ++i) {
            const unsigned char a = bitmap[i];
            m_AtlasRGBA[i * 4 + 0] = 255;
            m_AtlasRGBA[i * 4 + 1] = 255;
            m_AtlasRGBA[i * 4 + 2] = 255;
            m_AtlasRGBA[i * 4 + 3] = a;
        }

        // Reserve an opaque-white BLOCK in the untouched bottom-right corner
        // (8x8 texels) so every mip level averages to solid white and stays
        // opaque. kSolidU/V point at its centre; sampling it yields alpha 1.
        for (int sy = kAtlasH - kSolidSpan; sy < kAtlasH; ++sy) {
            for (int sx = kAtlasW - kSolidSpan; sx < kAtlasW; ++sx) {
                const size_t idx = static_cast<size_t>(sy) * kAtlasW + sx;
                m_AtlasRGBA[idx * 4 + 0] = 255;
                m_AtlasRGBA[idx * 4 + 1] = 255;
                m_AtlasRGBA[idx * 4 + 2] = 255;
                m_AtlasRGBA[idx * 4 + 3] = 255;
            }
        }

        // Build glyph lookup, insetting UVs by half a texel to sample
        // texel-center to texel-center (avoids LINEAR blending into neighbours).
        for (int i = 0; i < kNumChars; ++i) {
            const auto& p = packed[i];
            UIFontGlyph g;
            float x0 = static_cast<float>(p.x0) + 0.5f;
            float x1 = static_cast<float>(p.x1) - 0.5f;
            float y0 = static_cast<float>(p.y0) + 0.5f;
            float y1 = static_cast<float>(p.y1) - 0.5f;
            if (x1 - x0 < 1.0f) { x0 = static_cast<float>(p.x0); x1 = static_cast<float>(p.x1); }
            if (y1 - y0 < 1.0f) { y0 = static_cast<float>(p.y0); y1 = static_cast<float>(p.y1); }
            g.uv0 = glm::vec2(x0 / kAtlasW, y0 / kAtlasH);
            g.uv1 = glm::vec2(x1 / kAtlasW, y1 / kAtlasH);
            // Metrics come from a 2x bake; halve them back to logical units.
            g.size = glm::vec2(p.x1 - p.x0, p.y1 - p.y0) * 0.5f;
            g.bearing = glm::vec2(p.xoff, p.yoff) * 0.5f;
            g.advance = p.xadvance * 0.5f;
            m_Glyphs[kFirstChar + i] = g;
        }

        // Fallback character (box): samples the solid-white texel so it's visible.
        {
            UIFontGlyph fb;
            fb.uv0 = glm::vec2(kSolidU, kSolidV);
            fb.uv1 = glm::vec2(kSolidU, kSolidV);
            fb.size = glm::vec2(fontSize * 0.55f);
            fb.bearing = glm::vec2(0.0f);
            fb.advance = fontSize * 0.55f;
            m_Fallback = fb;
        }

        return true;
    }

    const UIFontGlyph& glyph(char32_t ch) const {
        if (ch >= kFirstChar && ch < 256u) {
            return m_Glyphs[static_cast<size_t>(ch)];
        }
        return m_Fallback;
    }

    float fontSize() const { return m_FontSize; }
    float scale() const { return m_Scale; }
    int atlasWidth() const { return kAtlasW; }
    int atlasHeight() const { return kAtlasH; }
    const unsigned char* atlasRGBA() const { return m_AtlasRGBA.data(); }
    size_t atlasByteCount() const { return m_AtlasRGBA.size(); }
    int atlasChannels() const { return 4; }

    // Measure a UTF-8 string width in pixels. Decodes multibyte sequences so
    // accented/Latin text advances correctly instead of byte-by-byte.
    float textWidth(const char* text) const {
        float w = 0.0f;
        const char* p = text;
        while (p && *p) {
            char32_t cp;
            p += utf8Decode(p, cp);
            w += glyph(cp).advance;
        }
        return w;
    }

    // Draw a UTF-8 string. topLeft is the top-left of the text block. scale
    // scales glyph metrics. If clipRightX > 0, characters starting at/after
    // that x are skipped (position-based horizontal clipping).
    void drawString(UIDrawList& dl, const std::string& text, const glm::vec2& topLeft,
                    float scale, const glm::vec4& color, float clipRightX = 0.0f) const {
        float penX = topLeft.x;
        const char* p = text.data();
        const char* end = text.data() + text.size();
        while (p < end) {
            char32_t cp;
            p += utf8Decode(p, cp);
            const UIFontGlyph& g = glyph(cp);
            const glm::vec2 glyphMin(penX + g.bearing.x * scale,
                                     topLeft.y + (ascentPx() + g.bearing.y) * scale);
            if (clipRightX > 0.0f && glyphMin.x >= clipRightX) break;
            dl.addQuad(glyphMin, glyphMin + g.size * scale, color, g.uv0, g.uv1);
            penX += g.advance * scale;
        }
    }

    // Decode one UTF-8 sequence starting at p. Returns bytes consumed (1-4) and
    // writes the codepoint to cp. Invalid/overlong sequences are replaced by the
    // raw byte value so they still render (via fallback) without crashing.
    static int utf8Decode(const char* p, char32_t& cp) {
        const unsigned char c0 = static_cast<unsigned char>(*p);
        if (c0 < 0x80) { cp = c0; return 1; }
        int n;
        char32_t v;
        if      ((c0 & 0xE0) == 0xC0) { n = 2; v = c0 & 0x1F; }
        else if ((c0 & 0xF0) == 0xE0) { n = 3; v = c0 & 0x0F; }
        else if ((c0 & 0xF8) == 0xF0) { n = 4; v = c0 & 0x07; }
        else { cp = c0; return 1; }
        for (int i = 1; i < n; ++i) {
            const unsigned char ci = static_cast<unsigned char>(p[i]);
            if ((ci & 0xC0) != 0x80) { cp = c0; return 1; } // bad continuation
            v = (v << 6) | (ci & 0x3F);
        }
        cp = v;
        return n;
    }

    float lineHeight() const { return m_LineHeight; }
    float ascentPx() const { return m_Ascent; }

private:
    stbtt_fontinfo m_Font{};
    float m_FontSize = 16.0f;
    float m_Scale = 1.0f;
    float m_Ascent = 0.0f;
    float m_LineHeight = 0.0f;
    std::vector<unsigned char> m_TTFData;  // owned copy; stbtt_fontinfo points here
    std::array<UIFontGlyph, 256> m_Glyphs{};
    UIFontGlyph m_Fallback;
    std::vector<unsigned char> m_AtlasRGBA;
};

} // namespace Atlas::UI
