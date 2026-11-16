#pragma once
// Vulkan 1.3 context: instance, device, swapchain, frame sync. vulkan-1.dll is
// loaded at runtime (no import library), so the exe starts under Wine/Proton
// and on machines without Vulkan (init() just returns false -> fall back to GL).
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <cstdint>
#include <functional>
#include <vector>

namespace foundry::rhi::vk {

#define FOUNDRY_VK_GLOBAL_FUNCS(X) X(vkCreateInstance)
#define FOUNDRY_VK_INSTANCE_FUNCS(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkEnumerateDeviceExtensionProperties) X(vkCreateWin32SurfaceKHR) X(vkDestroySurfaceKHR) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkCreateDevice) X(vkGetDeviceProcAddr)
#define FOUNDRY_VK_DEVICE_FUNCS(X) \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) X(vkCreateImageView) X(vkDestroyImageView) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkResetCommandBuffer) \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateFence) X(vkDestroyFence) \
    X(vkWaitForFences) X(vkResetFences) X(vkAcquireNextImageKHR) X(vkQueueSubmit2) \
    X(vkQueuePresentKHR) X(vkDeviceWaitIdle) X(vkCmdPipelineBarrier2) \
    X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) \
    X(vkDestroyPipeline) X(vkCmdBindPipeline) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdDraw) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) \
    X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCmdCopyImageToBuffer) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
    X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdDrawIndexed) X(vkCmdPushConstants) \
    X(vkCmdBindDescriptorSets) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) \
    X(vkUpdateDescriptorSets) X(vkCreateSampler) X(vkDestroySampler) X(vkCmdCopyBufferToImage) X(vkCmdBlitImage) \
    X(vkFreeCommandBuffers) X(vkCmdCopyBuffer) X(vkCmdClearColorImage)

struct Fns {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
#define X(n) PFN_##n n = nullptr;
    FOUNDRY_VK_GLOBAL_FUNCS(X) FOUNDRY_VK_INSTANCE_FUNCS(X) FOUNDRY_VK_DEVICE_FUNCS(X)
#undef X
};

constexpr int kFramesInFlight = 2;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

struct Image {
    VkImage image = nullptr;
    VkDeviceMemory memory = nullptr;
    VkImageView view = nullptr;
};

// Host-visible, persistently mapped buffer (fine for per-frame dynamic data;
// static geometry should move to device-local + staging later).
struct Buffer {
    VkBuffer buffer = nullptr;
    VkDeviceMemory memory = nullptr;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

struct Frame {
    VkCommandBuffer cmd = nullptr;
    VkImage image = nullptr;
    VkImageView view = nullptr;
    VkImage depthImage = nullptr;
    VkImageView depthView = nullptr;
    VkImageView msaaView = nullptr;      // multisampled colour (resolved into `view` at the end of the pass); null without MSAA
    VkExtent2D extent{};
    uint32_t imageIndex = 0;
};

class Context {
public:
    ~Context() { shutdown(); }
    // hwnd/hinstance are the Win32 handles. Returns false (with a message on stderr) if
    // Vulkan 1.3 is unavailable.
    bool init(void* hwnd, void* hinstance, uint32_t width, uint32_t height, bool enableValidation = false);
    void shutdown();
    // Recreate the swapchain (call on window resize). Returns false while minimized.
    bool resize(uint32_t width, uint32_t height);
    // vsync on = FIFO; off = IMMEDIATE (or MAILBOX if that is all there is). Rebuilds the swapchain when it changes.
    bool setVsync(bool on);
    bool vsync() const { return vsync_; }
    // Multisampling of the window pass (1 or 4; 4 is clamped to what the device supports). Rebuilds the swapchain targets.
    bool setSamples(int n);
    int samples() const { return int(samples_); }

    // Acquires an image and begins recording; the image is ready for rendering
    // (layout COLOR_ATTACHMENT_OPTIMAL). Returns false if the frame must be skipped.
    bool beginFrame(Frame& f);
    void endFrame(const Frame& f);   // presents (and captures if requested)

    // Ask for the next presented frame to be copied to CPU memory (RGBA8).
    void requestCapture() { capture_ = true; }
    // Read back the image presented by the last endFrame (what raylib's TakeScreenshot sees outside a frame).
    bool captureLastPresented(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);
    bool takeCapture(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);

    int frameIndex() const { return cur_; }   // frame-in-flight slot being recorded
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out);
    void destroyBuffer(Buffer& b);
    // Device-local buffer filled from `data` through a staging copy (static geometry). `mapped` stays null.
    bool createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out);
    // Device-local depth image that can also be sampled (shadow maps).
    bool createDepthImage(uint32_t w, uint32_t h, Image& out);
    void destroyImage(Image& img);
    void imageBarrier(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to,
                      VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                      VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                      VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t baseMip = 0, uint32_t mips = 1);
    // RGBA8 sampled colour image with `mips` levels (also a blit source/destination).
    bool createColorImage(uint32_t w, uint32_t h, uint32_t mips, Image& out);
    // Record and run a command buffer to completion (blocking). For uploads, not for per-frame work.
    bool submitOneTime(const std::function<void(VkCommandBuffer)>& record);
    uint32_t uboAlign() const { return uint32_t(limits_.minUniformBufferOffsetAlignment); }

    VkFormat colorFormat() const { return format_; }
    VkExtent2D extent() const { return extent_; }
    VkDevice device() const { return dev_; }
    const Fns& fn() const { return f_; }
    const char* deviceName() const { return deviceName_; }

private:
    bool loadLibrary();
    bool createSwapchain(uint32_t w, uint32_t h);
    void destroySwapchain();
    bool findMemoryType(uint32_t bits, VkMemoryPropertyFlags props, uint32_t& out) const;

    void* lib_ = nullptr;
    Fns f_;
    VkInstance inst_ = nullptr;
    VkPhysicalDevice phys_ = nullptr;
    VkDevice dev_ = nullptr;
    VkSurfaceKHR surface_ = nullptr;
    VkQueue queue_ = nullptr;
    uint32_t queueFamily_ = 0;
    VkPhysicalDeviceMemoryProperties memProps_{};
    VkPhysicalDeviceLimits limits_{};
    char deviceName_[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE]{};

    bool vsync_ = true;
    VkSwapchainKHR swapchain_ = nullptr;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> renderDone_;   // per swapchain image
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlags supportedSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkImage msaaImage_ = nullptr;
    VkDeviceMemory msaaMem_ = nullptr;
    VkImageView msaaView_ = nullptr;
    VkImage depthImage_ = nullptr;
    VkDeviceMemory depthMem_ = nullptr;
    VkImageView depthView_ = nullptr;

    VkCommandPool pool_ = nullptr;
    VkCommandBuffer cmds_[kFramesInFlight]{};
    VkSemaphore acquire_[kFramesInFlight]{};
    VkFence fence_[kFramesInFlight]{};
    int cur_ = 0;
    int lastPresented_ = -1;

    bool capture_ = false, captured_ = false;
    VkBuffer capBuf_ = nullptr; VkDeviceMemory capMem_ = nullptr;
    VkDeviceSize capSize_ = 0;
    std::vector<uint8_t> capPixels_; uint32_t capW_ = 0, capH_ = 0;
};

} // namespace foundry::rhi::vk
