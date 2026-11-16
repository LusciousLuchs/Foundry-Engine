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

#include "render/PortedShaders.inc"

static constexpr uint32_t kShadowRes = 2048;
static constexpr VkDeviceSize kVertBytes = 40u << 20, kUboBytes = 8u << 20;
static constexpr uint32_t kDynRange = 4096;        // largest uniform block a custom shader may use
static constexpr uint32_t kMaxTextures = 2048;

static uint64_t fnv1a(const char* s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (; s && *s; ++s) { h ^= uint8_t(*s); h *= 0x100000001b3ull; }
    return h;
}

const PortedShader* findPortedShader(const char* vs, const char* fs) {
    uint64_t hv = fnv1a(vs), hf = fnv1a(fs);
    for (const PortedShader& p : kPortedShaders)
        if (p.vsHash == hv && p.fsHash == hf) return &p;
    return nullptr;
}

const PortedShader* findPortedShaderByName(const char* name) {
    for (const PortedShader& p : kPortedShaders) if (!std::strcmp(p.name, name)) return &p;
    return nullptr;
}

// GL clip space -> Vulkan: y flipped, z from [-w,w] to [0,w]. (out = Conv * in, GLSL column-major.)
static void clipFix(const float a[16], float o[16]) {
    for (int c = 0; c < 4; ++c) {
        float x = a[4 * c], y = a[4 * c + 1], z = a[4 * c + 2], w = a[4 * c + 3];
        o[4 * c] = x; o[4 * c + 1] = -y; o[4 * c + 2] = 0.5f * z + 0.5f * w; o[4 * c + 3] = w;
    }
}

// Render-target passes keep GL orientation (row 0 = bottom): only the depth range changes.
static void clipFixRT(const float a[16], float o[16]) {
    for (int c = 0; c < 4; ++c) {
        float x = a[4 * c], y = a[4 * c + 1], z = a[4 * c + 2], w = a[4 * c + 3];
        o[4 * c] = x; o[4 * c + 1] = y; o[4 * c + 2] = 0.5f * z + 0.5f * w; o[4 * c + 3] = w;
    }
}

static VkShaderModule makeModule(const Context& c, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes; ci.pCode = code;
    VkShaderModule m = nullptr;
    c.fn().vkCreateShaderModule(c.device(), &ci, nullptr, &m);
    return m;
}

// ------------------------------------------------------------------------------------- init
bool Renderer::init(platform::Window& win) {
    win_ = &win;
    if (!ctx_.init(win.nativeHandle(), win.nativeInstance(), uint32_t(win.width()), uint32_t(win.height()))) return false;
    const Fns& f = ctx_.fn();
    VkDevice dev = ctx_.device();
    maxVerts_ = uint32_t(kVertBytes / sizeof(Vertex));
    uboSize_ = uint32_t(kUboBytes);

    // descriptor layouts / pools
    VkDescriptorSetLayoutBinding lb[2] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2; dl.pBindings = lb;
    if (f.vkCreateDescriptorSetLayout(dev, &dl, nullptr, &lightLayout_) != VK_SUCCESS) return false;
    VkDescriptorSetLayoutBinding db{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    dl.bindingCount = 1; dl.pBindings = &db;
    if (f.vkCreateDescriptorSetLayout(dev, &dl, nullptr, &dynLayout_) != VK_SUCCESS) return false;
    VkDescriptorSetLayoutBinding tb{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    dl.pBindings = &tb;
    if (f.vkCreateDescriptorSetLayout(dev, &dl, nullptr, &texLayout_) != VK_SUCCESS) return false;

    auto makePool = [&](VkDescriptorPool& pool, VkDescriptorType t1, uint32_t n1, VkDescriptorType t2, uint32_t n2, uint32_t maxSets) {
        VkDescriptorPoolSize ps[2] = {{t1, n1}, {t2, n2}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = maxSets; dp.poolSizeCount = n2 ? 2 : 1; dp.pPoolSizes = ps;
        return f.vkCreateDescriptorPool(dev, &dp, nullptr, &pool) == VK_SUCCESS;
    };
    if (!makePool(lightPool_, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight, kFramesInFlight) ||
        !makePool(dynPool_, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kFramesInFlight, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, kFramesInFlight) ||
        !makePool(texPool_, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, kMaxTextures)) return false;

    // shadow map + sampler
    if (!ctx_.createDepthImage(kShadowRes, kShadowRes, shadow_)) return false;
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR; sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sci.compareEnable = VK_TRUE; sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (f.vkCreateSampler(dev, &sci, nullptr, &shadowSampler_) != VK_SUCCESS) return false;

    for (auto& r : res_) {
        if (!ctx_.createBuffer(kVertBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, r.verts) ||
            !ctx_.createBuffer(kUboBytes + kDynRange, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, r.ubo) ||
            !ctx_.createBuffer(sizeof(LightsUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, r.lights)) return false;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = lightPool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &lightLayout_;
        if (f.vkAllocateDescriptorSets(dev, &ai, &r.lightSet) != VK_SUCCESS) return false;
        ai.descriptorPool = dynPool_; ai.pSetLayouts = &dynLayout_;
        if (f.vkAllocateDescriptorSets(dev, &ai, &r.dynSet) != VK_SUCCESS) return false;
        VkDescriptorBufferInfo lbi{r.lights.buffer, 0, sizeof(LightsUBO)}, dbi{r.ubo.buffer, 0, kDynRange};
        VkDescriptorImageInfo ii{shadowSampler_, shadow_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[3]{};
        w[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, r.lightSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &lbi, nullptr};
        w[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, r.lightSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, nullptr, nullptr};
        w[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, r.dynSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &dbi, nullptr};
        f.vkUpdateDescriptorSets(dev, 3, w, 0, nullptr);
    }
    if (!createPipelineLayouts()) return false;

    // built-in white 1x1 texture = id 1 (raylib's default texture)
    const uint8_t white[4] = {255, 255, 255, 255};
    if (createTexture(1, 1, white, false) != 1) return false;

    setSun(-0.45f, -1.0f, -0.35f, 1.0f, 0.95f, 0.85f, 1.6f);
    const float sky[3] = {0.16f, 0.21f, 0.32f}, gnd[3] = {0.06f, 0.05f, 0.05f};
    setAmbient(sky, gnd);
    std::printf("renderer: %s\n", ctx_.deviceName());
    return true;
}

bool Renderer::createPipelineLayouts() {
    const Fns& f = ctx_.fn();
    VkDevice dev = ctx_.device();
    mods_[0] = makeModule(ctx_, lit_vert_spv, sizeof lit_vert_spv);
    mods_[1] = makeModule(ctx_, lit_frag_spv, sizeof lit_frag_spv);
    mods_[2] = makeModule(ctx_, ui_vert_spv, sizeof ui_vert_spv);
    mods_[3] = makeModule(ctx_, ui_frag_spv, sizeof ui_frag_spv);
    mods_[4] = makeModule(ctx_, shadow_vert_spv, sizeof shadow_vert_spv);

    VkPushConstantRange litPc{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80};
    VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    l.setLayoutCount = 1; l.pSetLayouts = &lightLayout_; l.pushConstantRangeCount = 1; l.pPushConstantRanges = &litPc;
    if (f.vkCreatePipelineLayout(dev, &l, nullptr, &litLayout_) != VK_SUCCESS) return false;
    VkPushConstantRange uiPc{VK_SHADER_STAGE_VERTEX_BIT, 0, 16};
    VkPipelineLayoutCreateInfo l2{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    l2.pushConstantRangeCount = 1; l2.pPushConstantRanges = &uiPc;
    if (f.vkCreatePipelineLayout(dev, &l2, nullptr, &uiLayout_) != VK_SUCCESS) return false;
    VkDescriptorSetLayout cs[2] = {dynLayout_, texLayout_};
    VkPipelineLayoutCreateInfo l3{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    l3.setLayoutCount = 2; l3.pSetLayouts = cs;
    if (f.vkCreatePipelineLayout(dev, &l3, nullptr, &customLayout_) != VK_SUCCESS) return false;
    VkPushConstantRange shPc{VK_SHADER_STAGE_VERTEX_BIT, 0, 64};
    VkPipelineLayoutCreateInfo l4{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    l4.pushConstantRangeCount = 1; l4.pPushConstantRanges = &shPc;
    if (f.vkCreatePipelineLayout(dev, &l4, nullptr, &shadowLayout_) != VK_SUCCESS) return false;

    // depth-only pipeline for the sun shadow map
    VkPipeline p = nullptr;
    {
        VkPipelineShaderStageCreateInfo st{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, mods_[4], "main", nullptr};
        VkVertexInputBindingDescription bd{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription at{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &bd; vin.vertexAttributeDescriptionCount = 1; vin.pVertexAttributeDescriptions = &at;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
        rs.depthBiasEnable = VK_TRUE; rs.depthBiasConstantFactor = 1.5f; rs.depthBiasSlopeFactor = 2.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dsy.dynamicStateCount = 2; dsy.pDynamicStates = dyn;
        VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        ri.depthAttachmentFormat = kDepthFormat;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.pNext = &ri; gp.stageCount = 1; gp.pStages = &st; gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb; gp.pDynamicState = &dsy; gp.layout = shadowLayout_;
        if (f.vkCreateGraphicsPipelines(dev, nullptr, 1, &gp, nullptr, &p) != VK_SUCCESS) return false;
    }
    shadowPipe_ = p;
    return true;
}

// ------------------------------------------------------------------------------------- pipelines
VkPipeline Renderer::getPipeline(const Cmd& c) {
    uint64_t key = uint64_t(c.kind) | uint64_t(c.topo) << 2 | uint64_t(c.st.blend) << 3 | uint64_t(c.st.depthTest) << 5 |
                   uint64_t(c.st.depthWrite) << 6 | uint64_t(c.st.cullBack && c.kind == kCustom) << 7 | uint64_t(c.pass != 0) << 40 | uint64_t(uint32_t(c.shader)) << 8;
    auto it = pipes_.find(key);
    if (it != pipes_.end()) return it->second;
    VkPipeline p = buildPipeline(c);
    pipes_[key] = p;
    return p;
}

VkPipeline Renderer::buildPipeline(const Cmd& c) {
    const Fns& f = ctx_.fn();
    VkShaderModule vs, fs; VkPipelineLayout layout;
    if (c.kind == kLit) { vs = mods_[0]; fs = mods_[1]; layout = litLayout_; }
    else if (c.kind == kUi) { vs = mods_[2]; fs = mods_[3]; layout = uiLayout_; }
    else { const Shader& s = shaders_[size_t(c.shader - 1)]; vs = s.vs; fs = s.fs; layout = customLayout_; }

    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkVertexInputBindingDescription bd{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription at[4] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 20}, {3, 0, VK_FORMAT_R8G8B8A8_UNORM, 32}};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &bd;
    vin.vertexAttributeDescriptionCount = 4; vin.pVertexAttributeDescriptions = at;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = c.topo == Topo::Lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
    rs.cullMode = (c.kind == kCustom && c.st.cullBack) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    // The y flip in clipFix keeps on-screen orientation (raylib's CCW front faces stay CCW). Render-target passes
    // skip the flip, so the same triangles look clockwise in framebuffer space.
    rs.frontFace = c.pass ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = c.st.depthTest; ds.depthWriteEnable = c.st.depthWrite; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    switch (c.st.blend) {
        case Blend::Off: break;
        case Blend::Alpha:
            cba.blendEnable = VK_TRUE; cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
        case Blend::Additive:
            cba.blendEnable = VK_TRUE; cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; break;
        case Blend::Multiplied:
            cba.blendEnable = VK_TRUE; cba.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
    }
    cba.colorBlendOp = cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dsy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dsy.dynamicStateCount = 2; dsy.pDynamicStates = dyn;
    VkFormat color = c.pass ? VK_FORMAT_R8G8B8A8_UNORM : ctx_.colorFormat();
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &color; ri.depthAttachmentFormat = kDepthFormat;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &ri; gp.stageCount = 2; gp.pStages = st; gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb; gp.pDynamicState = &dsy; gp.layout = layout;
    VkPipeline p = nullptr;
    if (f.vkCreateGraphicsPipelines(ctx_.device(), nullptr, 1, &gp, nullptr, &p) != VK_SUCCESS) {
        std::fprintf(stderr, "renderer: pipeline creation failed\n");
        return nullptr;
    }
    return p;
}

// ------------------------------------------------------------------------------------- shutdown
void Renderer::shutdown() {
    if (!ctx_.device()) return;
    const Fns& f = ctx_.fn();
    VkDevice dev = ctx_.device();
    f.vkDeviceWaitIdle(dev);
    for (auto& m : meshes_) for (auto& b : m.buf) ctx_.destroyBuffer(b);
    for (auto& d : dead_) { for (auto& b : d) ctx_.destroyBuffer(b); d.clear(); }
    meshes_.clear();
    for (auto& r : res_) { ctx_.destroyBuffer(r.verts); ctx_.destroyBuffer(r.ubo); ctx_.destroyBuffer(r.lights); }
    for (auto& kv : pipes_) if (kv.second) f.vkDestroyPipeline(dev, kv.second, nullptr);
    pipes_.clear();
    if (shadowPipe_) f.vkDestroyPipeline(dev, shadowPipe_, nullptr);
    for (VkPipelineLayout l : {litLayout_, uiLayout_, customLayout_, shadowLayout_}) if (l) f.vkDestroyPipelineLayout(dev, l, nullptr);
    for (auto m : mods_) if (m) f.vkDestroyShaderModule(dev, m, nullptr);
    for (auto& s : shaders_) { if (s.vs) f.vkDestroyShaderModule(dev, s.vs, nullptr); if (s.fs) f.vkDestroyShaderModule(dev, s.fs, nullptr); }
    for (auto& t : textures_) { if (t.sampler) f.vkDestroySampler(dev, t.sampler, nullptr); if (!t.external) ctx_.destroyImage(t.img); }
    for (auto& t : targets_) { ctx_.destroyImage(t.color); ctx_.destroyImage(t.depth); }
    targets_.clear();
    if (shadowSampler_) f.vkDestroySampler(dev, shadowSampler_, nullptr);
    ctx_.destroyImage(shadow_);
    for (VkDescriptorPool p : {lightPool_, dynPool_, texPool_}) if (p) f.vkDestroyDescriptorPool(dev, p, nullptr);
    for (VkDescriptorSetLayout l : {lightLayout_, dynLayout_, texLayout_}) if (l) f.vkDestroyDescriptorSetLayout(dev, l, nullptr);
    shaders_.clear(); textures_.clear();
    litLayout_ = uiLayout_ = customLayout_ = shadowLayout_ = nullptr; shadowPipe_ = nullptr; shadowSampler_ = nullptr;
    lightPool_ = dynPool_ = texPool_ = nullptr; lightLayout_ = dynLayout_ = texLayout_ = nullptr;
    for (auto& m : mods_) m = nullptr;
    ctx_.shutdown();
}

// ------------------------------------------------------------------------------------- shaders
int Renderer::createShader(const PortedShader& p) {
    if (p.blockSize > kDynRange) { std::fprintf(stderr, "shader %s: uniform block too large\n", p.name); return 0; }
    const Fns& f = ctx_.fn();
    Shader s;
    s.src = &p;
    s.vs = makeModule(ctx_, p.vs, p.vsBytes);
    s.fs = makeModule(ctx_, p.fs, p.fsBytes);
    if (!s.vs || !s.fs) return 0;
    s.block.assign(p.blockSize ? p.blockSize : 16, 0);
    for (int i = 0; i < p.nUniforms; ++i) {
        const ShaderUniformInfo& u = p.uniforms[i];
        int off = int(u.offset);
        if (!std::strcmp(u.name, "mvp")) s.offMvp = off;
        else if (!std::strcmp(u.name, "matModel")) s.offModel = off;
        else if (!std::strcmp(u.name, "matNormal")) s.offNormal = off;
        else if (!std::strcmp(u.name, "matView")) s.offView = off;
        else if (!std::strcmp(u.name, "matProjection")) s.offProj = off;
        else if (!std::strcmp(u.name, "colDiffuse")) s.offDiffuse = off;
        else if (!std::strcmp(u.name, "_screen")) s.offScreen = off;
    }
    (void)f;
    shaders_.push_back(std::move(s));
    return int(shaders_.size());
}

void Renderer::destroyShader(int id) {
    if (id < 1 || size_t(id) > shaders_.size()) return;
    const Fns& f = ctx_.fn();
    Shader& s = shaders_[size_t(id - 1)];
    ctx_.fn().vkDeviceWaitIdle(ctx_.device());
    if (s.vs) f.vkDestroyShaderModule(ctx_.device(), s.vs, nullptr);
    if (s.fs) f.vkDestroyShaderModule(ctx_.device(), s.fs, nullptr);
    s.vs = s.fs = nullptr; s.src = nullptr;   // slot stays (ids are stable); pipelines for it are leaked until shutdown
}

uint8_t* Renderer::shaderBlock(int id) {
    return (id < 1 || size_t(id) > shaders_.size()) ? nullptr : shaders_[size_t(id - 1)].block.data();
}
int Renderer::uniformIndex(int id, const char* name) const {
    if (id < 1 || size_t(id) > shaders_.size() || !shaders_[size_t(id - 1)].src) return -1;
    const PortedShader* p = shaders_[size_t(id - 1)].src;
    for (int i = 0; i < p->nUniforms; ++i) if (!std::strcmp(p->uniforms[i].name, name)) return i;
    return -1;
}
const ShaderUniformInfo* Renderer::uniformInfo(int id, int index) const {
    if (id < 1 || size_t(id) > shaders_.size() || !shaders_[size_t(id - 1)].src) return nullptr;
    const PortedShader* p = shaders_[size_t(id - 1)].src;
    return (index < 0 || index >= p->nUniforms) ? nullptr : &p->uniforms[index];
}

// ------------------------------------------------------------------------------------- textures
bool Renderer::makeSampler(Texture& t) {
    const Fns& f = ctx_.fn();
    if (t.sampler) f.vkDestroySampler(ctx_.device(), t.sampler, nullptr);
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    bool point = t.filter == 0;
    sci.magFilter = sci.minFilter = point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    sci.mipmapMode = t.filter >= 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    static const VkSamplerAddressMode wraps[4] = {VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                  VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE};
    sci.addressModeU = sci.addressModeV = sci.addressModeW = wraps[t.wrap & 3];
    sci.maxLod = t.useMips ? float(t.mips - 1) : 0.0f;
    if (f.vkCreateSampler(ctx_.device(), &sci, nullptr, &t.sampler) != VK_SUCCESS) return false;
    VkDescriptorImageInfo ii{t.sampler, t.img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = t.set; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &ii;
    f.vkUpdateDescriptorSets(ctx_.device(), 1, &w, 0, nullptr);
    return true;
}

int Renderer::createTexture(int w, int h, const void* rgba, bool mipmaps) {
    if (w < 1 || h < 1 || textures_.size() >= kMaxTextures) return 0;
    const Fns& f = ctx_.fn();
    uint32_t mips = 1;
    { int m = std::max(w, h); while (m > 1) { m >>= 1; ++mips; } }   // the chain always exists; sampling it is opt-in (as in raylib)
    Texture t;
    t.w = w; t.h = h; t.mips = int(mips); t.filter = 1; t.useMips = mipmaps;
    if (!ctx_.createColorImage(uint32_t(w), uint32_t(h), mips, t.img)) return 0;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = texPool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &texLayout_;
    if (f.vkAllocateDescriptorSets(ctx_.device(), &ai, &t.set) != VK_SUCCESS) return 0;

    Buffer stage;
    VkDeviceSize bytes = VkDeviceSize(w) * VkDeviceSize(h) * 4;
    if (!ctx_.createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stage)) return 0;
    if (rgba) std::memcpy(stage.mapped, rgba, size_t(bytes)); else std::memset(stage.mapped, 0, size_t(bytes));
    constexpr VkPipelineStageFlags2 kXfer = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    ctx_.submitOneTime([&](VkCommandBuffer cmd) {
        ctx_.imageBarrier(cmd, t.img.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          kXfer, 0, kXfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, mips);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {uint32_t(w), uint32_t(h), 1};
        f.vkCmdCopyBufferToImage(cmd, stage.buffer, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        int32_t mw = w, mh = h;
        for (uint32_t i = 1; i < mips; ++i) {
            ctx_.imageBarrier(cmd, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              kXfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, kXfer, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1);
            VkImageBlit b{};
            b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
            b.srcOffsets[1] = {mw, mh, 1};
            int32_t nw = std::max(1, mw / 2), nh = std::max(1, mh / 2);
            b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
            b.dstOffsets[1] = {nw, nh, 1};
            f.vkCmdBlitImage(cmd, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
            mw = nw; mh = nh;
        }
        if (mips > 1)
            ctx_.imageBarrier(cmd, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              kXfer, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, 0, mips - 1);
        ctx_.imageBarrier(cmd, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          kXfer, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, mips - 1, 1);
    });
    ctx_.destroyBuffer(stage);
    if (!makeSampler(t)) return 0;
    t.used = true;
    textures_.push_back(t);
    return int(textures_.size());
}

void Renderer::updateTexture(int id, int x, int y, int w, int h, const void* pixels, int pitch) {
    if (id < 1 || size_t(id) > textures_.size() || !textures_[size_t(id - 1)].used || w < 1 || h < 1 || !pixels) return;
    Texture& t = textures_[size_t(id - 1)];
    if (x < 0 || y < 0 || x + w > t.w || y + h > t.h) return;
    Buffer stage;
    if (!ctx_.createBuffer(VkDeviceSize(w) * VkDeviceSize(h) * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stage)) return;
    for (int r = 0; r < h; ++r) std::memcpy(static_cast<uint8_t*>(stage.mapped) + size_t(r) * size_t(w) * 4, static_cast<const uint8_t*>(pixels) + size_t(r) * size_t(pitch), size_t(w) * 4);
    const Fns& f = ctx_.fn();
    ctx_.submitOneTime([&](VkCommandBuffer cmd) {
        ctx_.imageBarrier(cmd, t.img.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageOffset = {x, y, 0}; c.imageExtent = {uint32_t(w), uint32_t(h), 1};
        f.vkCmdCopyBufferToImage(cmd, stage.buffer, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        ctx_.imageBarrier(cmd, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
    });
    ctx_.destroyBuffer(stage);
}

void Renderer::destroyTexture(int id) {
    if (id <= 1 || size_t(id) > textures_.size() || !textures_[size_t(id - 1)].used) return;
    Texture& t = textures_[size_t(id - 1)];
    ctx_.fn().vkDeviceWaitIdle(ctx_.device());
    if (t.sampler) ctx_.fn().vkDestroySampler(ctx_.device(), t.sampler, nullptr);
    if (!t.external) ctx_.destroyImage(t.img);
    t.sampler = nullptr; t.used = false;
}
void Renderer::generateMipmaps(int id) {
    if (id < 1 || size_t(id) > textures_.size() || !textures_[size_t(id - 1)].used) return;
    ctx_.fn().vkDeviceWaitIdle(ctx_.device());
    textures_[size_t(id - 1)].useMips = true; makeSampler(textures_[size_t(id - 1)]);
}
void Renderer::setTextureFilter(int id, int filter) {
    if (id < 1 || size_t(id) > textures_.size() || !textures_[size_t(id - 1)].used) return;
    ctx_.fn().vkDeviceWaitIdle(ctx_.device());
    textures_[size_t(id - 1)].filter = filter; makeSampler(textures_[size_t(id - 1)]);
}
void Renderer::setTextureWrap(int id, int wrap) {
    if (id < 1 || size_t(id) > textures_.size() || !textures_[size_t(id - 1)].used) return;
    ctx_.fn().vkDeviceWaitIdle(ctx_.device());
    textures_[size_t(id - 1)].wrap = wrap; makeSampler(textures_[size_t(id - 1)]);
}

// ------------------------------------------------------------------------------------- frame
bool Renderer::beginFrame(float r, float g, float b, float a) {
    if (win_->consumeResized() && !win_->minimized()) ctx_.resize(uint32_t(win_->width()), uint32_t(win_->height()));
    if (win_->minimized()) return false;
    if (!ctx_.beginFrame(frame_)) { ctx_.resize(uint32_t(win_->width()), uint32_t(win_->height())); return false; }
    slot_ = ctx_.frameIndex();
    for (auto& b : dead_[slot_]) ctx_.destroyBuffer(b);   // this slot's previous frames have finished
    dead_[slot_].clear();
    passes_.assign(1, Pass{});
    curPass_ = 0;
    setClear(r, g, b, a);
    nVerts_ = 0; uboUsed_ = 0; nPoint_ = 0; lights_.counts[0] = 0;
    cmds_.clear(); segs_.clear(); curSeg_ = -1; overlay_ = false; inFrame_ = true;
    st_ = State{};
    return true;
}

void Renderer::setClear(float r, float g, float b, float a) {
    if (passes_.empty()) return;
    Pass& p = passes_[size_t(curPass_)];
    p.clear[0] = r; p.clear[1] = g; p.clear[2] = b; p.clear[3] = a; p.clearSet = true;
}
int Renderer::targetWidth() const {
    if (curPass_ > 0 && size_t(curPass_) < passes_.size()) return targets_[size_t(passes_[size_t(curPass_)].target - 1)].w;
    return int(frame_.extent.width);
}
int Renderer::targetHeight() const {
    if (curPass_ > 0 && size_t(curPass_) < passes_.size()) return targets_[size_t(passes_[size_t(curPass_)].target - 1)].h;
    return int(frame_.extent.height);
}

Renderer::State Renderer::drawState() const {
    State s = st_;
    if (overlay_) { s.depthTest = false; s.depthWrite = false; }
    return s;
}

bool Renderer::sameBatch(const Cmd& a, const Cmd& b) const {
    return a.kind == b.kind && a.kind != kCustom && a.topo == b.topo && a.segment == b.segment && a.overlay == b.overlay && a.pass == b.pass &&
           a.st.vpOn == b.st.vpOn && (!a.st.vpOn || !std::memcmp(a.st.vp, b.st.vp, sizeof a.st.vp)) &&
           a.st.depthTest == b.st.depthTest && a.st.depthWrite == b.st.depthWrite && a.st.blend == b.st.blend &&
           a.st.scissorOn == b.st.scissorOn && (!a.st.scissorOn || (!std::memcmp(a.st.sc, b.st.sc, sizeof a.st.sc))) &&
           a.first + a.count == b.first;
}

void Renderer::pushCmd(const Cmd& c) {
    if (!cmds_.empty() && sameBatch(cmds_.back(), c)) { cmds_.back().count += c.count; return; }
    cmds_.push_back(c);
}

Vertex* Renderer::reserve(uint32_t n) {
    if (nVerts_ + n > maxVerts_) { dropped_ += n; return nullptr; }
    Vertex* p = static_cast<Vertex*>(res_[slot_].verts.mapped) + nVerts_;
    return p;
}

void Renderer::begin3D(const float vp[16], float cx, float cy, float cz, bool overlay) {
    if (!inFrame_) return;
    std::vector<float> s(20);
    std::memcpy(s.data(), vp, 64);
    s[16] = cx; s[17] = cy; s[18] = cz; s[19] = curPass_ ? -1.0f : 1.0f;   // w < 0: no y flip (render target)
    segs_.push_back(std::move(s));
    curSeg_ = int(segs_.size()) - 1;
    overlay_ = overlay;
    st_.depthTest = true;          // like raylib's BeginMode3D
}
void Renderer::end3D() { overlay_ = false; curSeg_ = -1; st_.depthTest = false; }

void Renderer::tri(const Vertex& a, const Vertex& b, const Vertex& c) {
    if (!inFrame_ || curSeg_ < 0) return;
    Vertex* p = reserve(3);
    if (!p) return;
    p[0] = a; p[1] = b; p[2] = c;
    Cmd cmd{}; cmd.kind = kLit; cmd.topo = Topo::Triangles; cmd.st = drawState(); cmd.overlay = overlay_;
    cmd.segment = curSeg_; cmd.first = nVerts_; cmd.count = 3; cmd.pass = uint8_t(curPass_);
    nVerts_ += 3; pushCmd(cmd);
}

void Renderer::line(const Vertex& a, const Vertex& b) {
    if (!inFrame_ || curSeg_ < 0) return;
    Vertex* p = reserve(2);
    if (!p) return;
    p[0] = a; p[1] = b;
    p[0].nx = p[0].ny = p[0].nz = p[1].nx = p[1].ny = p[1].nz = 0;   // unlit
    Cmd cmd{}; cmd.kind = kLit; cmd.topo = Topo::Lines; cmd.st = drawState(); cmd.overlay = overlay_;
    cmd.segment = curSeg_; cmd.first = nVerts_; cmd.count = 2; cmd.pass = uint8_t(curPass_);
    nVerts_ += 2; pushCmd(cmd);
}

void Renderer::tri2D(const Vertex2D& a, const Vertex2D& b, const Vertex2D& c) {
    if (!inFrame_) return;
    Vertex* p = reserve(3);
    if (!p) return;
    const Vertex2D* in[3] = {&a, &b, &c};
    for (int i = 0; i < 3; ++i) p[i] = {in[i]->x, in[i]->y, 0, 0, 0, 0, 0, 0, in[i]->r, in[i]->g, in[i]->b, in[i]->a};
    Cmd cmd{}; cmd.kind = kUi; cmd.topo = Topo::Triangles; cmd.st = st_;
    cmd.st.depthTest = cmd.st.depthWrite = false; cmd.pass = uint8_t(curPass_);
    cmd.first = nVerts_; cmd.count = 3;
    nVerts_ += 3; pushCmd(cmd);
}

uint32_t Renderer::snapshotUniforms(int shader, const StdUniforms& u) {
    Shader& s = shaders_[size_t(shader - 1)];
    const uint32_t align = std::max(ctx_.uboAlign(), 16u);
    const uint32_t slice = (uint32_t(s.block.size()) + align - 1) / align * align;
    if (uboUsed_ + slice > uboSize_) return ~0u;
    uint8_t* dst = static_cast<uint8_t*>(res_[slot_].ubo.mapped) + uboUsed_;
    std::memcpy(dst, s.block.data(), s.block.size());
    float conv[16];
    auto put = [&](int off, const void* src, size_t bytes) { if (off >= 0) std::memcpy(dst + off, src, bytes); };
    if (curPass_) clipFixRT(u.mvp, conv); else clipFix(u.mvp, conv);
    put(s.offMvp, conv, 64);
    put(s.offModel, u.model, 64); put(s.offNormal, u.normal, 64);
    put(s.offView, u.view, 64); put(s.offProj, u.proj, 64);
    put(s.offDiffuse, u.diffuse, 16);
    float screen[2] = {float(targetWidth()), float(targetHeight())};
    put(s.offScreen, screen, 8);
    uint32_t off = uboUsed_;
    uboUsed_ += slice;
    return off;
}

void Renderer::drawCustom(int shader, const Vertex* v, uint32_t n, Topo topo, const StdUniforms& u, int texture) {
    if (!inFrame_ || shader < 1 || size_t(shader) > shaders_.size() || !shaders_[size_t(shader - 1)].src || n == 0) return;
    if (nVerts_ + n > maxVerts_) { dropped_ += n; return; }
    uint32_t off = snapshotUniforms(shader, u);
    if (off == ~0u) { dropped_ += n; return; }
    std::memcpy(static_cast<Vertex*>(res_[slot_].verts.mapped) + nVerts_, v, size_t(n) * sizeof(Vertex));
    Cmd c{}; c.kind = kCustom; c.topo = topo; c.st = st_; c.shader = shader; c.texture = texture > 0 ? texture : 1;
    c.first = nVerts_; c.count = n; c.uboOffset = off; c.pass = uint8_t(curPass_);
    nVerts_ += n;
    cmds_.push_back(c);
}

// ------------------------------------------------------------------------------------- render targets
int Renderer::createRenderTarget(int w, int h, int* textureId) {
    if (w < 1 || h < 1 || textures_.size() >= kMaxTextures) return 0;
    const Fns& f = ctx_.fn();
    Target t;
    t.w = w; t.h = h;
    if (!ctx_.createColorImage(uint32_t(w), uint32_t(h), 1, t.color)) return 0;
    if (!ctx_.createDepthImage(uint32_t(w), uint32_t(h), t.depth)) return 0;
    // colour image becomes a sampled texture (owned by the target)
    Texture tex;
    tex.img = t.color; tex.w = w; tex.h = h; tex.mips = 1; tex.filter = 1; tex.wrap = 1; tex.external = true;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = texPool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &texLayout_;
    if (f.vkAllocateDescriptorSets(ctx_.device(), &ai, &tex.set) != VK_SUCCESS) return 0;
    if (!makeSampler(tex)) return 0;
    tex.used = true;
    textures_.push_back(tex);
    t.texture = int(textures_.size());
    // start defined: cleared to transparent black and readable
    ctx_.submitOneTime([&](VkCommandBuffer cmd) {
        ctx_.imageBarrier(cmd, t.color.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkClearColorValue z{};
        VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        f.vkCmdClearColorImage(cmd, t.color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &z, 1, &r);
        ctx_.imageBarrier(cmd, t.color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    t.used = true;
    if (textureId) *textureId = t.texture;
    for (size_t i = 0; i < targets_.size(); ++i)
        if (!targets_[i].used) { targets_[i] = t; return int(i) + 1; }
    targets_.push_back(t);
    return int(targets_.size());
}

void Renderer::destroyRenderTarget(int id) {
    if (id < 1 || size_t(id) > targets_.size() || !targets_[size_t(id - 1)].used) return;
    Target& t = targets_[size_t(id - 1)];
    ctx_.fn().vkDeviceWaitIdle(ctx_.device());
    if (t.texture) {
        Texture& tx = textures_[size_t(t.texture - 1)];
        if (tx.sampler) ctx_.fn().vkDestroySampler(ctx_.device(), tx.sampler, nullptr);
        tx.sampler = nullptr; tx.used = false;
    }
    ctx_.destroyImage(t.color); ctx_.destroyImage(t.depth);
    t = Target{};
}

void Renderer::beginTarget(int id) {
    if (!inFrame_ || id < 1 || size_t(id) > targets_.size() || !targets_[size_t(id - 1)].used || passes_.size() >= 250) return;
    Pass p; p.target = id;
    passes_.push_back(p);
    curPass_ = int(passes_.size()) - 1;
    st_ = State{};            // each target starts from default state, like a fresh render context
    curSeg_ = -1; overlay_ = false;
}
void Renderer::endTarget() {
    if (curPass_ == 0) return;
    curPass_ = 0;
    st_ = State{};
    curSeg_ = -1; overlay_ = false;
}

// ------------------------------------------------------------------------------------- GPU meshes
int Renderer::createMesh(const Vertex* v, uint32_t n, bool dynamic) {
    if (!n) return 0;
    GpuMesh m;
    m.count = n; m.dynamic = dynamic; m.used = true;
    if (dynamic) {
        m.cpu.assign(v, v + n);
        for (int i = 0; i < kFramesInFlight; ++i) {
            if (!ctx_.createBuffer(VkDeviceSize(n) * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m.buf[i])) return 0;
            std::memcpy(m.buf[i].mapped, v, size_t(n) * sizeof(Vertex));
        }
    } else if (!ctx_.createDeviceBuffer(v, VkDeviceSize(n) * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m.buf[0])) return 0;
    // reuse a freed slot if there is one
    for (size_t i = 0; i < meshes_.size(); ++i)
        if (!meshes_[i].used) { meshes_[i] = std::move(m); return int(i) + 1; }
    meshes_.push_back(std::move(m));
    return int(meshes_.size());
}

void Renderer::updateMeshAttribute(int id, int attrib, const void* data, size_t bytes, size_t byteOffset) {
    if (id < 1 || size_t(id) > meshes_.size() || !meshes_[size_t(id - 1)].used || !data) return;
    GpuMesh& m = meshes_[size_t(id - 1)];
    if (!m.dynamic) return;   // static meshes are immutable (upload with dynamic=true to update)
    size_t elem = attrib == 0 || attrib == 2 ? 12 : attrib == 1 ? 8 : attrib == 3 ? 4 : 0;
    if (!elem) return;
    size_t first = byteOffset / elem, n = std::min(bytes / elem, m.cpu.size() > first ? m.cpu.size() - first : 0);
    const uint8_t* src = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        Vertex& d = m.cpu[first + i];
        const uint8_t* e = src + i * elem;
        if (attrib == 0) std::memcpy(&d.x, e, 12);
        else if (attrib == 1) std::memcpy(&d.u, e, 8);
        else if (attrib == 2) std::memcpy(&d.nx, e, 12);
        else std::memcpy(&d.r, e, 4);
    }
    for (bool& d : m.dirty) d = true;
}

void Renderer::destroyMesh(int id) {
    if (id < 1 || size_t(id) > meshes_.size() || !meshes_[size_t(id - 1)].used) return;
    GpuMesh& m = meshes_[size_t(id - 1)];
    for (auto& b : m.buf) if (b.buffer) dead_[slot_].push_back(b);   // the GPU may still be reading them
    m = GpuMesh{};
}

void Renderer::drawCustomMesh(int shader, int mesh, Topo topo, const StdUniforms& u, int texture) {
    if (!inFrame_ || shader < 1 || size_t(shader) > shaders_.size() || !shaders_[size_t(shader - 1)].src) return;
    if (mesh < 1 || size_t(mesh) > meshes_.size() || !meshes_[size_t(mesh - 1)].used) return;
    GpuMesh& m = meshes_[size_t(mesh - 1)];
    if (m.dynamic && m.dirty[slot_]) {   // refresh this frame slot's copy (its previous use has retired)
        std::memcpy(m.buf[slot_].mapped, m.cpu.data(), m.cpu.size() * sizeof(Vertex));
        m.dirty[slot_] = false;
    }
    uint32_t off = snapshotUniforms(shader, u);
    if (off == ~0u) { dropped_ += m.count; return; }
    Cmd c{}; c.kind = kCustom; c.topo = topo; c.st = st_; c.shader = shader; c.texture = texture > 0 ? texture : 1;
    c.mesh = mesh; c.first = 0; c.count = m.count; c.uboOffset = off; c.pass = uint8_t(curPass_);
    cmds_.push_back(c);
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
        Matrix lvp = MatrixMultiply(MatrixLookAt(eye, c, up), MatrixOrtho(-shadowR_, shadowR_, -shadowR_, shadowR_, 0.1, shadowR_ * 4.0));
        float16 m = MatrixToFloatV(lvp);
        std::memcpy(lights_.shadowVP, m.v, 64);
        lights_.shadowParams[0] = shadowOn_ ? 1.0f : 0.0f;
        lights_.shadowParams[1] = 1.0f / float(kShadowRes);
        lights_.shadowParams[2] = 2.0f * shadowR_ / float(kShadowRes);

        constexpr VkPipelineStageFlags2 kFrag = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        ctx_.imageBarrier(cmd, shadow_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, kFrag, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        VkRenderingAttachmentInfo sd{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        sd.imageView = shadow_.view; sd.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        sd.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; sd.storeOp = VK_ATTACHMENT_STORE_OP_STORE; sd.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo sri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        sri.renderArea = {{0, 0}, {kShadowRes, kShadowRes}}; sri.layerCount = 1; sri.pDepthAttachment = &sd;
        f.vkCmdBeginRendering(cmd, &sri);
        VkViewport svp{0, 0, float(kShadowRes), float(kShadowRes), 0, 1};
        VkRect2D ssc{{0, 0}, {kShadowRes, kShadowRes}};
        f.vkCmdSetViewport(cmd, 0, 1, &svp);
        f.vkCmdSetScissor(cmd, 0, 1, &ssc);
        if (shadowOn_ && nVerts_) {
            VkDeviceSize off = 0;
            f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipe_);
            f.vkCmdPushConstants(cmd, shadowLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, m.v);
            f.vkCmdBindVertexBuffers(cmd, 0, 1, &R.verts.buffer, &off);
            for (const Cmd& c2 : cmds_)   // only opaque built-in lit geometry casts shadows
                if (c2.pass == 0 && c2.kind == kLit && c2.topo == Topo::Triangles && c2.st.depthTest && c2.st.depthWrite && !c2.overlay)
                    f.vkCmdDraw(cmd, c2.count, 1, c2.first, 0);
        }
        f.vkCmdEndRendering(cmd);
        ctx_.imageBarrier(cmd, shadow_.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    std::memcpy(R.lights.mapped, &lights_, sizeof lights_);

    // ---- passes: render targets first (in call order), then the window ----
    constexpr VkPipelineStageFlags2 kColorOut = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    constexpr VkPipelineStageFlags2 kFragTests = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    auto drawPass = [&](int passIndex, uint32_t W, uint32_t Hh) {
        VkRect2D fullSc{{0, 0}, {W, Hh}};
        VkViewport fullVp{0, 0, float(W), float(Hh), 0, 1};
        f.vkCmdSetViewport(cmd, 0, 1, &fullVp);
        f.vkCmdSetScissor(cmd, 0, 1, &fullSc);
        VkDeviceSize zero = 0;
        f.vkCmdBindVertexBuffers(cmd, 0, 1, &R.verts.buffer, &zero);
        VkBuffer curVb = R.verts.buffer;
        VkPipeline bound = nullptr; int lastSeg = -2, lastTex = -1; Kind lastKind = Kind(255);
        bool scissored = false, vpCustom = false; int lastSc[4] = {-1, -1, -1, -1}, lastVp[4] = {-1, -1, -1, -1};
        float uiPc[3] = {float(W), float(Hh), passIndex ? -1.0f : 1.0f};
        for (const Cmd& c : cmds_) {
            if (c.pass != passIndex) continue;
            bool want = c.st.scissorOn;
            if (want != scissored || (want && std::memcmp(lastSc, c.st.sc, sizeof lastSc))) {
                VkRect2D r = fullSc;
                if (want) {
                    int x = std::max(0, c.st.sc[0]), y = std::max(0, c.st.sc[1]);
                    r = {{x, y}, {uint32_t(std::max(0, c.st.sc[2])), uint32_t(std::max(0, c.st.sc[3]))}};
                    std::memcpy(lastSc, c.st.sc, sizeof lastSc);
                }
                f.vkCmdSetScissor(cmd, 0, 1, &r);
                scissored = want;
            }
            if (c.st.vpOn != vpCustom || (c.st.vpOn && std::memcmp(lastVp, c.st.vp, sizeof lastVp))) {
                VkViewport v = fullVp;
                if (c.st.vpOn) { v = {float(c.st.vp[0]), float(c.st.vp[1]), float(c.st.vp[2]), float(c.st.vp[3]), 0, 1}; std::memcpy(lastVp, c.st.vp, sizeof lastVp); }
                f.vkCmdSetViewport(cmd, 0, 1, &v);
                vpCustom = c.st.vpOn;
            }
            VkPipeline p = getPipeline(c);
            if (!p) continue;
            if (p != bound) { f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p); bound = p; lastKind = Kind(255); lastSeg = -2; lastTex = -1; }
            if (c.kind == kLit) {
                if (lastKind != kLit) f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, litLayout_, 0, 1, &R.lightSet, 0, nullptr);
                if (c.segment != lastSeg) {
                    f.vkCmdPushConstants(cmd, litLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, segs_[size_t(c.segment)].data());
                    lastSeg = c.segment;
                }
            } else if (c.kind == kUi) {
                if (lastKind != kUi) f.vkCmdPushConstants(cmd, uiLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 12, uiPc);
            } else {
                f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, customLayout_, 0, 1, &R.dynSet, 1, &c.uboOffset);
                if (c.texture != lastTex) {
                    f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, customLayout_, 1, 1, &textures_[size_t(c.texture - 1)].set, 0, nullptr);
                    lastTex = c.texture;
                }
            }
            lastKind = c.kind;
            VkBuffer wantVb = R.verts.buffer;
            if (c.mesh) { const GpuMesh& gm = meshes_[size_t(c.mesh - 1)]; wantVb = gm.dynamic ? gm.buf[slot_].buffer : gm.buf[0].buffer; }
            if (wantVb != curVb) { f.vkCmdBindVertexBuffers(cmd, 0, 1, &wantVb, &zero); curVb = wantVb; }
            f.vkCmdDraw(cmd, c.count, 1, c.first, 0);
        }
    };

    for (size_t pi = 1; pi < passes_.size(); ++pi) {
        const Pass& pass = passes_[pi];
        Target& t = targets_[size_t(pass.target - 1)];
        ctx_.imageBarrier(cmd, t.color.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, kColorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
        ctx_.imageBarrier(cmd, t.depth.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                          kFragTests, 0, kFragTests, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        VkRenderingAttachmentInfo tc{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        tc.imageView = t.color.view; tc.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        tc.loadOp = pass.clearSet ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD; tc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        tc.clearValue.color = {{pass.clear[0], pass.clear[1], pass.clear[2], pass.clear[3]}};
        VkRenderingAttachmentInfo td{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        td.imageView = t.depth.view; td.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        td.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; td.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; td.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo tri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        tri.renderArea = {{0, 0}, {uint32_t(t.w), uint32_t(t.h)}}; tri.layerCount = 1;
        tri.colorAttachmentCount = 1; tri.pColorAttachments = &tc; tri.pDepthAttachment = &td;
        f.vkCmdBeginRendering(cmd, &tri);
        drawPass(int(pi), uint32_t(t.w), uint32_t(t.h));
        f.vkCmdEndRendering(cmd);
        ctx_.imageBarrier(cmd, t.color.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          kColorOut, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }

    VkRenderingAttachmentInfo col{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    col.imageView = frame_.view; col.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    col.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; col.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    col.clearValue.color = {{passes_[0].clear[0], passes_[0].clear[1], passes_[0].clear[2], passes_[0].clear[3]}};
    VkRenderingAttachmentInfo dep{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    dep.imageView = frame_.depthView; dep.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    dep.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; dep.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; dep.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, frame_.extent}; ri.layerCount = 1;
    ri.colorAttachmentCount = 1; ri.pColorAttachments = &col; ri.pDepthAttachment = &dep;
    f.vkCmdBeginRendering(cmd, &ri);
    drawPass(0, frame_.extent.width, frame_.extent.height);
    f.vkCmdEndRendering(cmd);

    bool shot = !shotPath_.empty();
    if (shot) ctx_.requestCapture();
    ctx_.endFrame(frame_);
    inFrame_ = false;
    if (dropped_) { std::fprintf(stderr, "renderer: dropped %u vertices (buffer full)\n", dropped_); dropped_ = 0; }
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
