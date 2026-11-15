// Vulkan hello-triangle on the Foundry window + context.
//   triangle.exe [--frames N] [--screenshot out.ppm]
#include "platform/Window.hpp"
#include "rhi/vulkan/VkContext.hpp"
#include "rhi/vulkan/shaders/triangle.vert.spv.h"
#include "rhi/vulkan/shaders/triangle.frag.spv.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace foundry;
using namespace foundry::rhi::vk;

static VkShaderModule makeModule(const Context& c, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes; ci.pCode = code;
    VkShaderModule m = nullptr;
    c.fn().vkCreateShaderModule(c.device(), &ci, nullptr, &m);
    return m;
}

int main(int argc, char** argv) {
    int maxFrames = -1;
    const char* shot = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
    }

    platform::Window win;
    if (!win.create("Foundry - triangle", 800, 600)) return 1;
    Context ctx;
    if (!ctx.init(win.nativeHandle(), win.nativeInstance(), win.width(), win.height())) {
        std::fprintf(stderr, "Vulkan init failed\n");
        return 2;
    }
    std::printf("device: %s\n", ctx.deviceName());

    const Fns& f = ctx.fn();
    VkDevice dev = ctx.device();
    VkShaderModule vs = makeModule(ctx, triangle_vert_spv, sizeof triangle_vert_spv);
    VkShaderModule fs = makeModule(ctx, triangle_frag_spv, sizeof triangle_frag_spv);
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout; f.vkCreatePipelineLayout(dev, &plci, nullptr, &layout);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{}; cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkFormat colorFmt = ctx.colorFormat();
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &colorFmt;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &rendering; gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia; gp.pViewportState = &vp;
    gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds; gp.layout = layout;
    VkPipeline pipeline = nullptr;
    if (f.vkCreateGraphicsPipelines(dev, nullptr, 1, &gp, nullptr, &pipeline) != VK_SUCCESS) {
        std::fprintf(stderr, "pipeline creation failed\n");
        return 3;
    }

    int frame = 0;
    bool wroteShot = false;
    while (!win.shouldClose() && (maxFrames < 0 || frame < maxFrames)) {
        win.pollEvents();
        if (win.isKeyPressed(platform::KEY_ESCAPE)) win.requestClose();
        if (win.consumeResized() && !win.minimized()) ctx.resize(win.width(), win.height());
        if (win.minimized()) continue;

        if (shot && frame == (maxFrames > 0 ? maxFrames - 1 : 5)) ctx.requestCapture();
        Frame fr;
        if (!ctx.beginFrame(fr)) { ctx.resize(win.width(), win.height()); continue; }

        VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        att.imageView = fr.view; att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.clearValue.color = {{0.05f, 0.06f, 0.09f, 1.0f}};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, fr.extent}; ri.layerCount = 1;
        ri.colorAttachmentCount = 1; ri.pColorAttachments = &att;
        f.vkCmdBeginRendering(fr.cmd, &ri);
        VkViewport v{0, 0, float(fr.extent.width), float(fr.extent.height), 0, 1};
        VkRect2D sc{{0, 0}, fr.extent};
        f.vkCmdSetViewport(fr.cmd, 0, 1, &v);
        f.vkCmdSetScissor(fr.cmd, 0, 1, &sc);
        f.vkCmdBindPipeline(fr.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        f.vkCmdDraw(fr.cmd, 3, 1, 0, 0);
        f.vkCmdEndRendering(fr.cmd);
        ctx.endFrame(fr);

        if (shot && !wroteShot) {
            std::vector<uint8_t> px; uint32_t w, h;
            if (ctx.takeCapture(px, w, h)) {
                if (FILE* o = std::fopen(shot, "wb")) {
                    std::fprintf(o, "P6\n%u %u\n255\n", w, h);
                    for (size_t i = 0; i < px.size(); i += 4) std::fwrite(&px[i], 1, 3, o);
                    std::fclose(o);
                    std::printf("wrote %s (%ux%u)\n", shot, w, h);
                }
                wroteShot = true;
            }
        }
        ++frame;
    }
    f.vkDeviceWaitIdle(dev);
    f.vkDestroyPipeline(dev, pipeline, nullptr);
    f.vkDestroyPipelineLayout(dev, layout, nullptr);
    f.vkDestroyShaderModule(dev, vs, nullptr);
    f.vkDestroyShaderModule(dev, fs, nullptr);
    std::printf("ok: %d frames\n", frame);
    return 0;
}
