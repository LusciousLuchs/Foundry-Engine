#pragma once
// Batched forward renderer on the Vulkan context. Immediate-mode in, one draw
// per pass out. Geometry arrives in world space; matrices follow raylib/OpenGL
// conventions (the shader converts to Vulkan clip space).
//
// Lighting: sun + hemisphere ambient + up to 8 point lights, Blinn-Phong,
// ACES tone mapping, sRGB output. Zero-normal vertices are drawn unlit.
#include "platform/Window.hpp"
#include "rhi/vulkan/VkContext.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace foundry::render {

struct Vertex3D { float x, y, z, nx, ny, nz; uint8_t r, g, b, a; };
struct Vertex2D { float x, y; uint8_t r, g, b, a; };
constexpr int kMaxPointLights = 8;

class Renderer {
public:
    ~Renderer() { shutdown(); }
    bool init(platform::Window& win);
    void shutdown();

    bool beginFrame(float r, float g, float b, float a);   // false: skip this frame
    void endFrame();
    void setClear(float r, float g, float b, float a) { clear_[0] = r; clear_[1] = g; clear_[2] = b; clear_[3] = a; }

    // 3D passes. viewProj is column-major as raylib stores it (MatrixToFloat order).
    // overlay = no depth test, drawn over everything so far (editor gizmos, markers).
    void begin3D(const float viewProj[16], float camX, float camY, float camZ, bool overlay = false);
    void end3D();
    void tri(const Vertex3D& a, const Vertex3D& b, const Vertex3D& c);
    void line(const Vertex3D& a, const Vertex3D& b);

    // 2D overlay (pixels, origin top-left), drawn after all 3D, no depth.
    void tri2D(const Vertex2D& a, const Vertex2D& b, const Vertex2D& c);

    // Lighting. Point lights reset every frame (immediate style).
    void setSun(float dx, float dy, float dz, float r, float g, float b, float intensity);
    void setAmbient(const float sky[3], const float ground[3]);
    bool addPointLight(float x, float y, float z, float range, float r, float g, float b, float intensity);

    // Sun shadows: orthographic map fitted to a sphere (center, radius) around the action.
    void setShadows(bool enabled, float cx, float cy, float cz, float radius) {
        shadowOn_ = enabled; shadowC_[0] = cx; shadowC_[1] = cy; shadowC_[2] = cz; shadowR_ = radius;
    }
    void setShadowsEnabled(bool on) { shadowOn_ = on; }
    bool shadowsEnabled() const { return shadowOn_; }

    void requestScreenshot(const std::string& path) { shotPath_ = path; }
    int width() const { return int(frame_.extent.width); }
    int height() const { return int(frame_.extent.height); }
    bool inFrame() const { return inFrame_; }
    rhi::vk::Context& context() { return ctx_; }
    uint32_t droppedVertices() const { return dropped_; }

private:
    struct Segment { float pc[20]; uint32_t triStart, triCount, lineStart, lineCount; bool overlay; };
    struct FrameRes {
        rhi::vk::Buffer tris, lines, ui, ubo;
        VkDescriptorSet set = nullptr;
    };
    bool createPipelines();

    platform::Window* win_ = nullptr;
    rhi::vk::Context ctx_;
    rhi::vk::Frame frame_;
    FrameRes res_[rhi::vk::kFramesInFlight];
    VkDescriptorSetLayout setLayout_ = nullptr;
    VkDescriptorPool pool_ = nullptr;
    VkPipelineLayout litLayout_ = nullptr, uiLayout_ = nullptr;
    VkPipeline triPipe_ = nullptr, linePipe_ = nullptr, uiPipe_ = nullptr, triOver_ = nullptr, lineOver_ = nullptr;
    VkShaderModule mods_[5]{};
    rhi::vk::Image shadow_;
    VkSampler shadowSampler_ = nullptr;
    VkPipelineLayout shadowLayout_ = nullptr;
    VkPipeline shadowPipe_ = nullptr;
    bool shadowOn_ = true;
    float shadowC_[3] = {0, 0, 0};
    float shadowR_ = 16.0f;

    bool inFrame_ = false, in3D_ = false;
    float clear_[4]{};
    int slot_ = 0;
    uint32_t nTri_ = 0, nLine_ = 0, nUi_ = 0, dropped_ = 0;
    uint32_t maxTri_ = 0, maxLine_ = 0, maxUi_ = 0;
    std::vector<Segment> segs_;
    std::string shotPath_;

    struct LightsUBO {
        float sunDir[4], sunColor[4], ambSky[4], ambGround[4], counts[4];
        float shadowVP[16], shadowParams[4];
        float pts[kMaxPointLights][8];
    } lights_{};
    int nPoint_ = 0;
};

} // namespace foundry::render
