#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOUSER
#define NOGDI
#define NOMINMAX
#include "ui/rlImGui.hpp"
#include "compat/internal.hpp"
#include "render/Renderer.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace foundry::compat {
namespace {

using render::Renderer;
using render::Vertex;

struct Backend {
    ImGuiContext* ctx = nullptr;
    int shader = 0;                       // raylib's default textured shader
    void (*loadFonts)() = nullptr;
    double lastTime = 0;
    bool keyDown[ImGuiKey_NamedKey_END]{};
} B;

struct KeyPair { int ours; ImGuiKey imgui; };
const KeyPair kKeys[] = {
    {KEY_SPACE, ImGuiKey_Space}, {KEY_APOSTROPHE, ImGuiKey_Apostrophe}, {KEY_COMMA, ImGuiKey_Comma}, {KEY_MINUS, ImGuiKey_Minus},
    {KEY_PERIOD, ImGuiKey_Period}, {KEY_SLASH, ImGuiKey_Slash}, {KEY_SEMICOLON, ImGuiKey_Semicolon}, {KEY_EQUAL, ImGuiKey_Equal},
    {KEY_LEFT_BRACKET, ImGuiKey_LeftBracket}, {KEY_BACKSLASH, ImGuiKey_Backslash}, {KEY_RIGHT_BRACKET, ImGuiKey_RightBracket},
    {KEY_GRAVE, ImGuiKey_GraveAccent}, {KEY_ESCAPE, ImGuiKey_Escape}, {KEY_ENTER, ImGuiKey_Enter}, {KEY_TAB, ImGuiKey_Tab},
    {KEY_BACKSPACE, ImGuiKey_Backspace}, {KEY_INSERT, ImGuiKey_Insert}, {KEY_DELETE, ImGuiKey_Delete}, {KEY_RIGHT, ImGuiKey_RightArrow},
    {KEY_LEFT, ImGuiKey_LeftArrow}, {KEY_DOWN, ImGuiKey_DownArrow}, {KEY_UP, ImGuiKey_UpArrow}, {KEY_PAGE_UP, ImGuiKey_PageUp},
    {KEY_PAGE_DOWN, ImGuiKey_PageDown}, {KEY_HOME, ImGuiKey_Home}, {KEY_END, ImGuiKey_End},
    {KEY_LEFT_SHIFT, ImGuiKey_LeftShift}, {KEY_LEFT_CONTROL, ImGuiKey_LeftCtrl}, {KEY_LEFT_ALT, ImGuiKey_LeftAlt},
    {KEY_RIGHT_SHIFT, ImGuiKey_RightShift}, {KEY_RIGHT_CONTROL, ImGuiKey_RightCtrl}, {KEY_RIGHT_ALT, ImGuiKey_RightAlt},
};

// Press and release can both land inside one frame (fast clicks/taps); send each event so none is lost.
void feedKey(ImGuiIO& io, int ours, ImGuiKey key) {
    if (IsKeyPressed(ours)) { io.AddKeyEvent(key, true); B.keyDown[key] = true; }
    if (IsKeyReleased(ours)) { io.AddKeyEvent(key, false); B.keyDown[key] = false; }
    bool down = IsKeyDown(ours);
    if (down != B.keyDown[key]) { io.AddKeyEvent(key, down); B.keyDown[key] = down; }   // e.g. focus loss
}

// ---- font atlas / texture requests (ImGui 1.92) ----
void handleTexture(ImTextureData* tex) {
    Renderer& r = internal::renderer();
    auto rgbaFor = [&](int x, int y, int w, int h, std::vector<unsigned char>& tmp) -> const void* {
        if (tex->BytesPerPixel == 4) return tex->GetPixelsAt(x, y);
        tmp.resize(size_t(w) * size_t(h) * 4);   // Alpha8 -> white with alpha
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                unsigned char a = *static_cast<unsigned char*>(tex->GetPixelsAt(x + i, y + j));
                unsigned char* o = &tmp[(size_t(j) * size_t(w) + size_t(i)) * 4];
                o[0] = o[1] = o[2] = 255; o[3] = a;
            }
        return tmp.data();
    };
    switch (tex->Status) {
        case ImTextureStatus_WantCreate: {
            std::vector<unsigned char> tmp;
            const void* px = rgbaFor(0, 0, tex->Width, tex->Height, tmp);
            int id = r.createTexture(tex->Width, tex->Height, px, false);
            if (id) { r.setTextureWrap(id, TEXTURE_WRAP_CLAMP); tex->SetTexID(ImTextureID(id)); tex->SetStatus(ImTextureStatus_OK); }
            break;
        }
        case ImTextureStatus_WantUpdates: {
            int id = int(tex->GetTexID());
            for (const ImTextureRect& rc : tex->Updates) {
                std::vector<unsigned char> tmp;
                const void* px = rgbaFor(rc.x, rc.y, rc.w, rc.h, tmp);
                r.updateTexture(id, rc.x, rc.y, rc.w, rc.h, px, tex->BytesPerPixel == 4 ? tex->GetPitch() : rc.w * 4);
            }
            tex->SetStatus(ImTextureStatus_OK);
            break;
        }
        case ImTextureStatus_WantDestroy:
            if (tex->UnusedFrames > 0) {
                r.destroyTexture(int(tex->GetTexID()));
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
            break;
        default: break;
    }
}

void renderDrawData(ImDrawData* dd) {
    Renderer& r = internal::renderer();
    if (!dd || !r.inFrame() || B.shader == 0) return;
    if (dd->Textures)
        for (ImTextureData* t : *dd->Textures)
            if (t->Status != ImTextureStatus_OK) handleTexture(t);
    if (dd->DisplaySize.x <= 0 || dd->DisplaySize.y <= 0) return;

    render::StdUniforms u{};
    {
        Matrix o = MatrixOrtho(0, double(dd->DisplaySize.x), double(dd->DisplaySize.y), 0, 0.0, 1.0);
        float16 f = MatrixToFloatV(o);
        std::memcpy(u.mvp, f.v, 64); std::memcpy(u.proj, f.v, 64);
        float16 id = MatrixToFloatV(MatrixIdentity());
        std::memcpy(u.model, id.v, 64); std::memcpy(u.normal, id.v, 64); std::memcpy(u.view, id.v, 64);
        u.diffuse[0] = u.diffuse[1] = u.diffuse[2] = u.diffuse[3] = 1.0f;
    }
    r.setCullBack(false);
    r.setDepthTest(false);
    std::vector<Vertex> verts;
    const ImVec2 off = dd->DisplayPos, scale = dd->FramebufferScale;
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* list = dd->CmdLists[n];
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback != ImDrawCallback_ResetRenderState) cmd.UserCallback(list, &cmd);
                continue;
            }
            ImVec2 a((cmd.ClipRect.x - off.x) * scale.x, (cmd.ClipRect.y - off.y) * scale.y);
            ImVec2 b((cmd.ClipRect.z - off.x) * scale.x, (cmd.ClipRect.w - off.y) * scale.y);
            a.x = std::max(a.x, 0.0f); a.y = std::max(a.y, 0.0f);
            if (b.x <= a.x || b.y <= a.y || cmd.ElemCount < 3) continue;
            r.setScissor(true, int(a.x), int(a.y), int(b.x - a.x + 0.999f), int(b.y - a.y + 0.999f));
            verts.resize(cmd.ElemCount);
            for (unsigned i = 0; i < cmd.ElemCount; ++i) {
                const ImDrawVert& v = list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset + i] + cmd.VtxOffset];
                verts[i] = {v.pos.x, v.pos.y, 0.0f, v.uv.x, v.uv.y, 0, 0, 1,
                            uint8_t(v.col & 0xFF), uint8_t((v.col >> 8) & 0xFF), uint8_t((v.col >> 16) & 0xFF), uint8_t(v.col >> 24)};
            }
            r.drawCustom(B.shader, verts.data(), uint32_t(verts.size()), render::Topo::Triangles, u, int(cmd.GetTexID()));
        }
    }
    r.setScissor(false, 0, 0, 0, 0);
    r.setCullBack(true);
}

} // namespace

void rlImGuiSetLoadFontsCallback(void (*cb)()) { B.loadFonts = cb; }

void rlImGuiSetup(bool dark) {
    if (B.ctx) return;
    B.ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = io.BackendRendererName = "foundry";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.IniFilename = nullptr;
    if (dark) ImGui::StyleColorsDark(); else ImGui::StyleColorsLight();
    if (const render::PortedShader* p = render::findPortedShaderByName("builtin_default")) B.shader = internal::renderer().createShader(*p);
    if (B.loadFonts) B.loadFonts();
    B.lastTime = GetTime();
}

void rlImGuiShutdown() {
    if (!B.ctx) return;
    ImGui::DestroyContext(B.ctx);
    B.ctx = nullptr;
    if (B.shader) { internal::renderer().destroyShader(B.shader); B.shader = 0; }
}

void rlImGuiBegin() {
    if (!B.ctx) return;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(GetScreenWidth()), float(GetScreenHeight()));
    io.DisplayFramebufferScale = ImVec2(1, 1);
    double now = GetTime();
    io.DeltaTime = float(std::max(now - B.lastTime, 1e-5));
    B.lastTime = now;

    Vector2 m = GetMousePosition();
    io.AddMousePosEvent(m.x, m.y);
    for (int b = 0; b < 3; ++b) {
        static bool down[3] = {false, false, false};
        if (IsMouseButtonPressed(b)) { io.AddMouseButtonEvent(b, true); down[b] = true; }
        if (IsMouseButtonReleased(b)) { io.AddMouseButtonEvent(b, false); down[b] = false; }
        bool d = IsMouseButtonDown(b);
        if (d != down[b]) { io.AddMouseButtonEvent(b, d); down[b] = d; }
    }
    float wheel = GetMouseWheelMove();
    if (wheel != 0) io.AddMouseWheelEvent(0, wheel);

    for (int k = KEY_ZERO; k <= KEY_NINE; ++k) feedKey(io, k, ImGuiKey(ImGuiKey_0 + (k - KEY_ZERO)));
    for (int k = KEY_A; k <= KEY_Z; ++k) feedKey(io, k, ImGuiKey(ImGuiKey_A + (k - KEY_A)));
    for (int k = KEY_F1; k <= KEY_F12; ++k) feedKey(io, k, ImGuiKey(ImGuiKey_F1 + (k - KEY_F1)));
    for (const KeyPair& kp : kKeys) feedKey(io, kp.ours, kp.imgui);
    io.AddKeyEvent(ImGuiMod_Ctrl, IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
    io.AddKeyEvent(ImGuiMod_Alt, IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT));
    for (int c; (c = GetCharPressed()) != 0;) io.AddInputCharacter(unsigned(c));

    ImGui::NewFrame();
}

void rlImGuiEnd() {
    if (!B.ctx) return;
    ImGui::Render();
    renderDrawData(ImGui::GetDrawData());
}

void rlImGuiImageSize(const Texture2D* image, int w, int h) {
    if (image) ImGui::Image(ImTextureRef(ImTextureID(image->id)), ImVec2(float(w), float(h)));
}

bool rlImGuiImageButtonSize(const char* name, const Texture2D* image, ImVec2 size) {
    return image && ImGui::ImageButton(name, ImTextureRef(ImTextureID(image->id)), size);
}

} // namespace foundry::compat
