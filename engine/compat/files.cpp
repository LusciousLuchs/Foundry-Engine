// Image loading/resizing, DEFLATE decompression and TrueType fonts for the compat layer.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOUSER
#define NOGDI
#define NOMINMAX
#include "compat/raylib.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#include "../../third_party/stb/stb_image.h"

#define SINFL_IMPLEMENTATION
#include "../../third_party/stb/sinfl.h"

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define IMSTB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/imgui/imstb_truetype.h"

namespace foundry::compat {

// ============================================================ images
Image LoadImageFromMemory(const char*, const unsigned char* data, int size) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(data, size, &w, &h, &n, 4);
    if (!px) return {nullptr, 0, 0, 0, 0};
    Image im{std::malloc(size_t(w) * size_t(h) * 4), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    std::memcpy(im.data, px, size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    return im;
}
Image LoadImage(const char* fileName) {
    std::ifstream f(fileName, std::ios::binary);
    if (!f) return {nullptr, 0, 0, 0, 0};
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return LoadImageFromMemory("", buf.data(), int(buf.size()));
}
Image GenImageChecked(int w, int h, int cx, int cy, Color a, Color b) {
    Image im{std::malloc(size_t(w) * size_t(h) * 4), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    auto* p = static_cast<unsigned char*>(im.data);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            Color c = ((x / std::max(1, cx) + y / std::max(1, cy)) % 2 == 0) ? a : b;   // raylib: checks are cx x cy pixels
            unsigned char* o = p + (size_t(y) * size_t(w) + size_t(x)) * 4;
            o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = c.a;
        }
    return im;
}
void ImageResize(Image* im, int nw, int nh) {
    if (!im || !im->data || nw < 1 || nh < 1) return;
    auto* src = static_cast<unsigned char*>(im->data);
    auto* dst = static_cast<unsigned char*>(std::malloc(size_t(nw) * size_t(nh) * 4));
    for (int y = 0; y < nh; ++y) {
        float fy = (float(y) + 0.5f) * float(im->height) / float(nh) - 0.5f;
        int y0 = std::clamp(int(std::floor(fy)), 0, im->height - 1), y1 = std::min(y0 + 1, im->height - 1);
        float ty = std::clamp(fy - float(y0), 0.0f, 1.0f);
        for (int x = 0; x < nw; ++x) {
            float fx = (float(x) + 0.5f) * float(im->width) / float(nw) - 0.5f;
            int x0 = std::clamp(int(std::floor(fx)), 0, im->width - 1), x1 = std::min(x0 + 1, im->width - 1);
            float tx = std::clamp(fx - float(x0), 0.0f, 1.0f);
            for (int c = 0; c < 4; ++c) {
                float a = src[(size_t(y0) * size_t(im->width) + size_t(x0)) * 4 + size_t(c)] * (1 - tx) + src[(size_t(y0) * size_t(im->width) + size_t(x1)) * 4 + size_t(c)] * tx;
                float b = src[(size_t(y1) * size_t(im->width) + size_t(x0)) * 4 + size_t(c)] * (1 - tx) + src[(size_t(y1) * size_t(im->width) + size_t(x1)) * 4 + size_t(c)] * tx;
                dst[(size_t(y) * size_t(nw) + size_t(x)) * 4 + size_t(c)] = (unsigned char)std::lround(a * (1 - ty) + b * ty);
            }
        }
    }
    std::free(im->data);
    im->data = dst; im->width = nw; im->height = nh;
}
void ImageFormat(Image*, int) {}

// ============================================================ compression
unsigned char* DecompressData(const unsigned char* comp, int compSize, int* outSize) {
    constexpr int kMaxMB = 64;   // raylib's MAX_DECOMPRESSION_SIZE
    auto* data = static_cast<unsigned char*>(std::calloc(size_t(kMaxMB) * 1024 * 1024, 1));
    if (!data) { if (outSize) *outSize = 0; return nullptr; }
    int len = sinflate(data, kMaxMB * 1024 * 1024, comp, compSize);
    if (len <= 0) { std::free(data); if (outSize) *outSize = 0; return nullptr; }
    if (auto* t = static_cast<unsigned char*>(std::realloc(data, size_t(len)))) data = t;
    if (outSize) *outSize = len;
    return data;
}

// ============================================================ fonts
Font LoadFontFromMemory(const char*, const unsigned char* data, int, int fontSize, int* codepoints, int codepointCount) {
    Font font{};
    stbtt_fontinfo info;
    if (!data || !stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0))) return font;
    std::vector<int> cps;
    if (codepoints && codepointCount > 0) cps.assign(codepoints, codepoints + codepointCount);
    else for (int c = 32; c < 127; ++c) cps.push_back(c);   // raylib's default set
    const int n = int(cps.size());
    int pad = 4;
    for (int size : {512, 1024, 2048, 4096}) {
        std::vector<unsigned char> alpha(size_t(size) * size_t(size), 0);
        std::vector<stbtt_packedchar> packed = std::vector<stbtt_packedchar>(static_cast<size_t>(n));
        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, alpha.data(), size, size, 0, pad, nullptr)) continue;
        stbtt_pack_range range{};
        range.font_size = float(fontSize); range.array_of_unicode_codepoints = cps.data(); range.num_chars = n; range.chardata_for_range = packed.data();
        bool ok = stbtt_PackFontRanges(&pc, data, 0, &range, 1) != 0;
        stbtt_PackEnd(&pc);
        if (!ok) continue;
        // white glyphs with alpha, ready for the default shader
        std::vector<unsigned char> rgba(size_t(size) * size_t(size) * 4);
        for (size_t i = 0; i < alpha.size(); ++i) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = alpha[i]; }
        Image atlas{rgba.data(), size, size, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        font.texture = LoadTextureFromImage(atlas);
        SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(font.texture, TEXTURE_WRAP_CLAMP);
        font.baseSize = fontSize; font.glyphCount = n; font.glyphPadding = pad;
        font.recs = static_cast<Rectangle*>(std::calloc(size_t(n), sizeof(Rectangle)));
        font.glyphs = static_cast<GlyphInfo*>(std::calloc(size_t(n), sizeof(GlyphInfo)));
        float scale = stbtt_ScaleForPixelHeight(&info, float(fontSize));
        int ascent; stbtt_GetFontVMetrics(&info, &ascent, nullptr, nullptr);
        for (int i = 0; i < n; ++i) {
            const stbtt_packedchar& g = packed[size_t(i)];
            font.recs[i] = {float(g.x0), float(g.y0), float(g.x1 - g.x0), float(g.y1 - g.y0)};
            font.glyphs[i].value = cps[size_t(i)];
            font.glyphs[i].offsetX = int(std::lround(g.xoff));
            font.glyphs[i].offsetY = int(std::lround(g.yoff + float(ascent) * scale));   // from the line top, like raylib
            font.glyphs[i].advanceX = int(std::lround(g.xadvance));
        }
        return font;
    }
    return font;
}
Font LoadFontEx(const char* fileName, int fontSize, int* codepoints, int codepointCount) {
    std::ifstream f(fileName, std::ios::binary);
    if (!f) return {};
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return LoadFontFromMemory("", buf.data(), int(buf.size()), fontSize, codepoints, codepointCount);
}
bool IsFontReady(Font f) { return f.texture.id != 0 && f.glyphs != nullptr; }
void UnloadFont(Font f) { UnloadTexture(f.texture); std::free(f.recs); std::free(f.glyphs); }

static int glyphIndex(const Font& f, int cp) {
    for (int i = 0; i < f.glyphCount; ++i) if (f.glyphs[i].value == cp) return i;
    for (int i = 0; i < f.glyphCount; ++i) if (f.glyphs[i].value == '?') return i;
    return 0;
}
// Decode one UTF-8 code point; returns bytes consumed.
static int nextCodepoint(const char* s, int& cp) {
    unsigned char c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) { cp = c; return 1; }
    if ((c >> 5) == 6 && s[1]) { cp = ((c & 0x1F) << 6) | (s[1] & 0x3F); return 2; }
    if ((c >> 4) == 14 && s[1] && s[2]) { cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); return 3; }
    if ((c >> 3) == 30 && s[1] && s[2] && s[3]) { cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); return 4; }
    cp = '?'; return 1;
}
Vector2 MeasureTextEx(Font f, const char* text, float size, float spacing) {
    if (!text || !IsFontReady(f)) return {0, 0};
    float scale = size / float(f.baseSize), w = 0, best = 0, lines = 1;
    for (int i = 0; text[i];) {
        int cp; i += nextCodepoint(text + i, cp);
        if (cp == '\n') { best = std::max(best, w); w = 0; lines += 1; continue; }
        w += float(f.glyphs[glyphIndex(f, cp)].advanceX) * scale + spacing;
    }
    best = std::max(best, w);
    return {best > 0 ? best - spacing : 0, size * lines + (lines - 1) * (size * 0.5f)};
}
void DrawTextEx(Font f, const char* text, Vector2 pos, float size, float spacing, Color tint) {
    if (!text || !IsFontReady(f)) return;
    float scale = size / float(f.baseSize), x = pos.x, y = pos.y;
    for (int i = 0; text[i];) {
        int cp; i += nextCodepoint(text + i, cp);
        if (cp == '\n') { x = pos.x; y += size + size * 0.5f; continue; }
        int gi = glyphIndex(f, cp);
        const Rectangle& r = f.recs[gi];
        const GlyphInfo& g = f.glyphs[gi];
        if (cp != ' ' && r.width > 0)
            DrawTexturePro(f.texture, r, {x + float(g.offsetX) * scale, y + float(g.offsetY) * scale, r.width * scale, r.height * scale}, {0, 0}, 0, tint);
        x += float(g.advanceX) * scale + spacing;
    }
}

} // namespace foundry::compat

// ============================================================ file-name helpers (raylib's rcore; raudio.c links to these)
extern "C" {
const char* GetFileExtension(const char* fileName) {
    if (!fileName) return nullptr;
    const char* dot = std::strrchr(fileName, '.');
    return (!dot || dot == fileName) ? nullptr : dot;
}
bool IsFileExtension(const char* fileName, const char* ext) {
    const char* e = fileName ? GetFileExtension(fileName) : nullptr;
    if (!e || !ext) return false;
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
    for (const char* p = ext; *p;) {   // ext may be a ';'-separated list, e.g. ".png;.jpg"
        const char* end = std::strchr(p, ';');
        size_t n = end ? size_t(end - p) : std::strlen(p);
        if (n == std::strlen(e)) {
            bool same = true;
            for (size_t i = 0; i < n; ++i) if (lower(p[i]) != lower(e[i])) { same = false; break; }
            if (same) return true;
        }
        if (!end) break;
        p = end + 1;
    }
    return false;
}
const char* GetFileName(const char* path) {
    if (!path) return nullptr;
    const char* a = std::strrchr(path, '\\');
    const char* b = std::strrchr(path, '/');
    const char* sep = a > b ? a : b;
    return sep ? sep + 1 : path;
}
const char* GetFileNameWithoutExt(const char* path) {
    static char name[256];
    std::memset(name, 0, sizeof name);
    if (path) {
        std::strncpy(name, GetFileName(path), sizeof name - 1);
        for (int i = int(std::strlen(name)); i > 0; --i) if (name[i] == '.') { name[i] = '\0'; break; }
    }
    return name;
}
} // extern "C"
