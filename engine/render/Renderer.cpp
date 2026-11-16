#include "render/Renderer.hpp"
#include "rhi/vulkan/shaders/lit.vert.spv.h"
#include "rhi/vulkan/shaders/lit.frag.spv.h"
#include "rhi/vulkan/shaders/ui.vert.spv.h"
#include "rhi/vulkan/shaders/ui.frag.spv.h"
#include "rhi/vulkan/shaders/shadow.vert.spv.h"
#include "compat/raymath.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace foundry::render {
using namespace rhi::vk;

static constexpr uint32_t kShadowRes = 2048;
static constexpr VkDeviceSize kTriBytes = 24u << 20, kLineBytes = 8u << 20, kUiBytes = 8u << 20;

bool Renderer::init(platform::Window& win) {
    win_ = &win;
    if (!ctx_.init(win.nativeHandle(), win.nativeInstance(), uint32_t(win.width()), uint32_t(win.height()))) return false;
    const Fns& f = ctx_.fn();
    VkDevice dev = ctx_.device();

    maxTri_ = uint32_t(kTriBytes / sizeof(Vertex3D));
    maxLine_ = uint32_t(kLineBytes / sizeof(Vertex3D));
    maxUi_ = uint32_t(kUiBytes / sizeof(Vertex2D));

    VkDescriptorSetLayoutBinding bind[2] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2; dl.pBindings = bind;
    if (f.vkCreateDescriptorSetLayout(dev, &dl, nullptr, &setLayout_) != VK_SUCCESS) return false;
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
                                  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = kFramesInFlight; dp.poolSizeCount = 2; dp.pPoolSizes = ps;

    if (!ctx_.createDepthImage(kShadowRes, kShadowRes, shadow_)) return false;
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR; sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sci.compareEnable = VK_TRUE; sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (f.vkCreateSampler(dev, &sci, nullptr, &shadowSampler_) != VK_SUCCESS) return false;
    if (f.vkCreateDescriptorPool(dev, &dp, nullptr, &pool_) != VK_SUCCESS) return false;

    for (auto& r : res_) {
        if (!ctx_.createBuffer(kTriBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, r.tris) ||
            !ctx_.createBuffer(kLineBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, r.lines) ||
            !ctx_.createBuffer(kUiBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, r.ui) ||
            !ctx_.createBuffer(sizeof(LightsUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, r.ubo)) return false;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &setLayout_;
        if (f.vkAllocateDescriptorSets(dev, &ai, &r.set) != VK_SUCCESS) return false;
        VkDescriptorBufferInfo bi{r.ubo.buffer, 0, sizeof(LightsUBO)};
        VkDescriptorImageInfo ii{shadowSampler_, shadow_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[2]{};
        w[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, r.set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bi, nullptr};
        w[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, r.set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, nullptr, nullptr};
        f.vkUpdateDescriptorSets(dev, 2, w, 0, nullptr);
    }
    if (!createPipelines()) return false;

    // Defaults: warm sun, cool sky / warm ground hemisphere.
    setSun(-0.45f, -1.0f, -0.35f, 1.0f, 0.95f, 0.85f, 1.6f);
    const float sky[3] = {0.16f, 0.21f, 0.32f}, gnd[3] = {0.06f, 0.05f, 0.05f};
    setAmbient(sky, gnd);
    std::printf("renderer: %s\n", ctx_.deviceName());
    return true;
}

static VkShaderModule mod(const Context& c, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes; ci.pCode = code;
    VkShaderModule m = nullptr;
    c.fn().vkCreateShaderModule(c.device(), &ci, nullptr, &m);
    return m;
}

bool Renderer::createPipelines() {
    const Fns& f = ctx_.fn();
    VkDevice dev = ctx_.device();
    mods_[0] = mod(ctx_, lit_vert_spv, sizeof lit_vert_spv);
    mods_[1] = mod(ctx_, lit_frag_spv, sizeof lit_frag_spv);
    mods_[2] = mod(ctx_, ui_vert_spv, sizeof ui_vert_spv);
    mods_[3] = mod(ctx_, ui_frag_spv, sizeof ui_frag_spv);
    mods_[4] = mod(ctx_, shadow_vert_spv, sizeof shadow_vert_spv);

    VkPushConstantRange litPc{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80};
    VkPipelineLayoutCreateInfo l1{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    l1.setLayoutCount = 1; l1.pSetLayouts = &setLayout_; l1.pushConstantRangeCount = 1; l1.pPushConstantRanges = &litPc;
    if (f.vkCreatePipelineLayout(dev, &l1, nullptr, &litLayout_) != VK_SUCCESS) return false;
    VkPushConstantRange uiPc{VK_SHADER_STAGE_VERTEX_BIT, 0, 8};
    VkPipelineLayoutCreateInfo l2{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    l2.pushConstantRangeCount = 1; l2.pPushConstantRanges = &uiPc;
    if (f.vkCreatePipelineLayout(dev, &l2, nullptr, &uiLayout_) != VK_SUCCESS) return false;

    VkPushConstantRange shPc{VK_SHADER_STAGE_VERTEX_BIT, 0, 64};
    VkPipelineLayoutCreateInfo l3{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    l3.pushConstantRangeCount = 1; l3.pPushConstantRanges = &shPc;
    if (f.vkCreatePipelineLayout(dev, &l3, nullptr, &shadowLayout_) != VK_SUCCESS) return false;

    auto build = [&](VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout, VkPrimitiveTopology topo,
                     bool is3D, VkPipeline& out, bool depthOnly = false, bool overlay = false) {
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
        st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
        VkVertexInputBindingDescription bd{0, uint32_t(is3D ? sizeof(Vertex3D) : sizeof(Vertex2D)), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription at3[3] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
                                                    {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 24}};
        VkVertexInputAttributeDescription at2[2] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R8G8B8A8_UNORM, 8}};
        VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &bd;
        vin.vertexAttributeDescriptionCount = depthOnly ? 1 : is3D ? 3 : 2; vin.pVertexAttributeDescriptions = is3D ? at3 : at2;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = topo;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
        if (depthOnly) { rs.depthBiasEnable = VK_TRUE; rs.depthBiasConstantFactor = 1.5f; rs.depthBiasSlopeFactor = 2.0f; }
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo dsS{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        dsS.depthTestEnable = is3D && !overlay; dsS.depthWriteEnable = is3D && !overlay; dsS.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        if (!is3D) {
            cba.blendEnable = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.colorBlendOp = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = depthOnly ? 0 : 1; cb.pAttachments = &cba;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dsy.dynamicStateCount = 2; dsy.pDynamicStates = dyn;
        VkFormat color = ctx_.colorFormat();
        VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        ri.colorAttachmentCount = depthOnly ? 0 : 1; ri.pColorAttachmentFormats = &color; ri.depthAttachmentFormat = kDepthFormat;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.pNext = &ri; gp.stageCount = depthOnly ? 1 : 2; gp.pStages = st; gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pDepthStencilState = &dsS;
        gp.pColorBlendState = &cb; gp.pDynamicState = &dsy; gp.layout = layout;
        return f.vkCreateGraphicsPipelines(dev, nullptr, 1, &gp, nullptr, &out) == VK_SUCCESS;
    };
    return build(mods_[0], mods_[1], litLayout_, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, triPipe_) &&
           build(mods_[0], mods_[1], litLayout_, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, linePipe_) &&
           build(mods_[0], mods_[1], litLayout_, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, triOver_, false, true) &&
           build(mods_[0], mods_[1], litLayout_, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, lineOver_, false, true) &&
           build(mods_[2], mods_[3], uiLayout_, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, uiPipe_) &&
           build(mods_[4], nullptr, shadowLayout_, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, shadowPipe_, true);
}

void Renderer::shutdown() {
    if (!ctx_.device()) return;
    const Fns& f = ctx_.fn();
    VkDevice dev = ctx_.device();
    f.vkDeviceWaitIdle(dev);
    for (auto& r : res_) { ctx_.destroyBuffer(r.tris); ctx_.destroyBuffer(r.lines); ctx_.destroyBuffer(r.ui); ctx_.destroyBuffer(r.ubo); }
    if (triPipe_) f.vkDestroyPipeline(dev, triPipe_, nullptr);
    if (linePipe_) f.vkDestroyPipeline(dev, linePipe_, nullptr);
    if (uiPipe_) f.vkDestroyPipeline(dev, uiPipe_, nullptr);
    if (shadowPipe_) f.vkDestroyPipeline(dev, shadowPipe_, nullptr);
    if (triOver_) f.vkDestroyPipeline(dev, triOver_, nullptr);
    if (lineOver_) f.vkDestroyPipeline(dev, lineOver_, nullptr);
    if (shadowLayout_) f.vkDestroyPipelineLayout(dev, shadowLayout_, nullptr);
    if (shadowSampler_) f.vkDestroySampler(dev, shadowSampler_, nullptr);
    ctx_.destroyImage(shadow_);
    if (litLayout_) f.vkDestroyPipelineLayout(dev, litLayout_, nullptr);
    if (uiLayout_) f.vkDestroyPipelineLayout(dev, uiLayout_, nullptr);
    for (auto m : mods_) if (m) f.vkDestroyShaderModule(dev, m, nullptr);
    if (pool_) f.vkDestroyDescriptorPool(dev, pool_, nullptr);
    if (setLayout_) f.vkDestroyDescriptorSetLayout(dev, setLayout_, nullptr);
    triPipe_ = linePipe_ = uiPipe_ = shadowPipe_ = triOver_ = lineOver_ = nullptr; shadowLayout_ = nullptr; shadowSampler_ = nullptr; litLayout_ = uiLayout_ = nullptr; pool_ = nullptr; setLayout_ = nullptr;
    for (auto& m : mods_) m = nullptr;
    ctx_.shutdown();
}

bool Renderer::beginFrame(float r, float g, float b, float a) {
    if (win_->consumeResized() && !win_->minimized()) ctx_.resize(uint32_t(win_->width()), uint32_t(win_->height()));
    if (win_->minimized()) return false;
    if (!ctx_.beginFrame(frame_)) { ctx_.resize(uint32_t(win_->width()), uint32_t(win_->height())); return false; }
    slot_ = ctx_.frameIndex();
    setClear(r, g, b, a);
    nTri_ = nLine_ = nUi_ = 0; nPoint_ = 0; lights_.counts[0] = 0;
    segs_.clear(); in3D_ = false; inFrame_ = true;
    return true;
}

void Renderer::begin3D(const float vp[16], float cx, float cy, float cz, bool overlay) {
    if (!inFrame_) return;
    if (in3D_) end3D();
    Segment s{};
    std::memcpy(s.pc, vp, 64);
    s.pc[16] = cx; s.pc[17] = cy; s.pc[18] = cz; s.pc[19] = 1;
    s.triStart = nTri_; s.lineStart = nLine_; s.overlay = overlay;
    segs_.push_back(s); in3D_ = true;
}

void Renderer::end3D() {
    if (!in3D_) return;
    Segment& s = segs_.back();
    s.triCount = nTri_ - s.triStart; s.lineCount = nLine_ - s.lineStart;
    in3D_ = false;
}

void Renderer::tri(const Vertex3D& a, const Vertex3D& b, const Vertex3D& c) {
    if (!in3D_) return;
    if (nTri_ + 3 > maxTri_) { dropped_ += 3; return; }
    auto* p = static_cast<Vertex3D*>(res_[slot_].tris.mapped) + nTri_;
    p[0] = a; p[1] = b; p[2] = c; nTri_ += 3;
}

void Renderer::line(const Vertex3D& a, const Vertex3D& b) {
    if (!in3D_) return;
    if (nLine_ + 2 > maxLine_) { dropped_ += 2; return; }
    auto* p = static_cast<Vertex3D*>(res_[slot_].lines.mapped) + nLine_;
    Vertex3D va = a, vb = b;
    va.nx = va.ny = va.nz = vb.nx = vb.ny = vb.nz = 0;   // unlit
    p[0] = va; p[1] = vb; nLine_ += 2;
}

void Renderer::tri2D(const Vertex2D& a, const Vertex2D& b, const Vertex2D& c) {
    if (!inFrame_) return;
    if (nUi_ + 3 > maxUi_) { dropped_ += 3; return; }
    auto* p = static_cast<Vertex2D*>(res_[slot_].ui.mapped) + nUi_;
    p[0] = a; p[1] = b; p[2] = c; nUi_ += 3;
}

void Renderer::setSun(float dx, float dy, float dz, float r, float g, float b, float k) {
    float l = std::sqrt(dx * dx + dy * dy + dz * dz); if (l == 0) l = 1;
    lights_.sunDir[0] = dx / l; lights_.sunDir[1] = dy / l; lights_.sunDir[2] = dz / l; lights_.sunDir[3] = 0;
    lights_.sunColor[0] = r * k; lights_.sunColor[1] = g * k; lights_.sunColor[2] = b * k; lights_.sunColor[3] = 0;
}

void Renderer::setAmbient(const float sky[3], const float gnd[3]) {
    for (int i = 0; i < 3; ++i) { lights_.ambSky[i] = sky[i]; lights_.ambGround[i] = gnd[i]; }
}

bool Renderer::addPointLight(float x, float y, float z, float range, float r, float g, float b, float k) {
    if (nPoint_ >= kMaxPointLights) return false;
    float* p = lights_.pts[nPoint_++];
    p[0] = x; p[1] = y; p[2] = z; p[3] = range; p[4] = r; p[5] = g; p[6] = b; p[7] = k;
    lights_.counts[0] = float(nPoint_);
    return true;
}

void Renderer::endFrame() {
    if (!inFrame_) return;
    if (in3D_) end3D();
    const Fns& f = ctx_.fn();
    VkCommandBuffer cmd = frame_.cmd;
    FrameRes& R = res_[slot_];

    // ---- sun shadow map ----
    {
        using namespace foundry::compat;
        Vector3 d = Vector3Normalize({lights_.sunDir[0], lights_.sunDir[1], lights_.sunDir[2]});
        Vector3 c{shadowC_[0], shadowC_[1], shadowC_[2]};
        Vector3 up = std::fabs(d.y) > 0.98f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
        Vector3 eye = Vector3Subtract(c, Vector3Scale(d, shadowR_ * 2.0f));
        Matrix lvp = MatrixMultiply(MatrixLookAt(eye, c, up),
                                    MatrixOrtho(-shadowR_, shadowR_, -shadowR_, shadowR_, 0.1, shadowR_ * 4.0));
        float16 m = MatrixToFloatV(lvp);
        std::memcpy(lights_.shadowVP, m.v, 64);
        lights_.shadowParams[0] = shadowOn_ ? 1.0f : 0.0f;
        lights_.shadowParams[1] = 1.0f / float(kShadowRes);
        lights_.shadowParams[2] = 2.0f * shadowR_ / float(kShadowRes);

        constexpr VkPipelineStageFlags2 kFrag = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        ctx_.imageBarrier(cmd, shadow_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, kFrag, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT);
        VkRenderingAttachmentInfo sd{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        sd.imageView = shadow_.view; sd.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        sd.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; sd.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        sd.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo sri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        sri.renderArea = {{0, 0}, {kShadowRes, kShadowRes}}; sri.layerCount = 1; sri.pDepthAttachment = &sd;
        f.vkCmdBeginRendering(cmd, &sri);
        VkViewport svp{0, 0, float(kShadowRes), float(kShadowRes), 0, 1};
        VkRect2D ssc{{0, 0}, {kShadowRes, kShadowRes}};
        f.vkCmdSetViewport(cmd, 0, 1, &svp);
        f.vkCmdSetScissor(cmd, 0, 1, &ssc);
        if (shadowOn_ && nTri_) {
            VkDeviceSize off = 0;
            f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipe_);
            f.vkCmdPushConstants(cmd, shadowLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, m.v);
            f.vkCmdBindVertexBuffers(cmd, 0, 1, &R.tris.buffer, &off);
            for (const Segment& sg : segs_)
                if (!sg.overlay && sg.triCount) f.vkCmdDraw(cmd, sg.triCount, 1, sg.triStart, 0);   // overlays cast no shadows
        }
        f.vkCmdEndRendering(cmd);
        ctx_.imageBarrier(cmd, shadow_.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    std::memcpy(R.ubo.mapped, &lights_, sizeof lights_);

    VkRenderingAttachmentInfo col{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    col.imageView = frame_.view; col.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    col.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; col.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    col.clearValue.color = {{clear_[0], clear_[1], clear_[2], clear_[3]}};
    VkRenderingAttachmentInfo dep{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    dep.imageView = frame_.depthView; dep.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    dep.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; dep.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    dep.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, frame_.extent}; ri.layerCount = 1;
    ri.colorAttachmentCount = 1; ri.pColorAttachments = &col; ri.pDepthAttachment = &dep;
    f.vkCmdBeginRendering(cmd, &ri);
    VkViewport vp{0, 0, float(frame_.extent.width), float(frame_.extent.height), 0, 1};
    VkRect2D sc{{0, 0}, frame_.extent};
    f.vkCmdSetViewport(cmd, 0, 1, &vp);
    f.vkCmdSetScissor(cmd, 0, 1, &sc);

    if (!segs_.empty()) {
        f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, litLayout_, 0, 1, &R.set, 0, nullptr);
        for (const Segment& s : segs_) {
            f.vkCmdPushConstants(cmd, litLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, s.pc);
            VkDeviceSize off = 0;
            if (s.triCount) {
                f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.overlay ? triOver_ : triPipe_);
                f.vkCmdBindVertexBuffers(cmd, 0, 1, &R.tris.buffer, &off);
                f.vkCmdDraw(cmd, s.triCount, 1, s.triStart, 0);
            }
            if (s.lineCount) {
                f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.overlay ? lineOver_ : linePipe_);
                f.vkCmdBindVertexBuffers(cmd, 0, 1, &R.lines.buffer, &off);
                f.vkCmdDraw(cmd, s.lineCount, 1, s.lineStart, 0);
            }
        }
    }
    if (nUi_) {
        float screen[2] = {float(frame_.extent.width), float(frame_.extent.height)};
        VkDeviceSize off = 0;
        f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipe_);
        f.vkCmdPushConstants(cmd, uiLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, screen);
        f.vkCmdBindVertexBuffers(cmd, 0, 1, &R.ui.buffer, &off);
        f.vkCmdDraw(cmd, nUi_, 1, 0, 0);
    }
    f.vkCmdEndRendering(cmd);

    bool shot = !shotPath_.empty();
    if (shot) ctx_.requestCapture();
    ctx_.endFrame(frame_);
    inFrame_ = false;
    if (dropped_) { std::fprintf(stderr, "renderer: dropped %u vertices (batch full)\n", dropped_); dropped_ = 0; }

    if (shot) {
        std::vector<uint8_t> px; uint32_t w, h;
        if (ctx_.takeCapture(px, w, h)) {
            if (FILE* o = std::fopen(shotPath_.c_str(), "wb")) {
                std::fprintf(o, "P6\n%u %u\n255\n", w, h);
                for (size_t i = 0; i < px.size(); i += 4) std::fwrite(&px[i], 1, 3, o);
                std::fclose(o);
                std::printf("screenshot: %s (%ux%u)\n", shotPath_.c_str(), w, h);
            }
        }
        shotPath_.clear();
    }
}

} // namespace foundry::render
