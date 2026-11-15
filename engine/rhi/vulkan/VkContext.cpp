#include "rhi/vulkan/VkContext.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace foundry::rhi::vk {

static void barrier(const Fns& f, VkCommandBuffer cmd, VkImage img,
                    VkImageLayout from, VkImageLayout to,
                    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = srcStage; b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage; b.dstAccessMask = dstAccess;
    b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    d.imageMemoryBarrierCount = 1; d.pImageMemoryBarriers = &b;
    f.vkCmdPipelineBarrier2(cmd, &d);
}

bool Context::loadLibrary() {
    HMODULE m = LoadLibraryA("vulkan-1.dll");
    if (!m) { std::fprintf(stderr, "vulkan-1.dll not found\n"); return false; }
    lib_ = m;
    f_.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        reinterpret_cast<void*>(GetProcAddress(m, "vkGetInstanceProcAddr")));
    if (!f_.vkGetInstanceProcAddr) return false;
#define X(n) f_.n = reinterpret_cast<PFN_##n>(f_.vkGetInstanceProcAddr(nullptr, #n));
    FOUNDRY_VK_GLOBAL_FUNCS(X)
#undef X
    return f_.vkCreateInstance != nullptr;
}

bool Context::findMemoryType(uint32_t bits, VkMemoryPropertyFlags props, uint32_t& out) const {
    for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & props) == props) { out = i; return true; }
    return false;
}

#define VKCHECK(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { \
    std::fprintf(stderr, "Vulkan error %d at %s:%d (%s)\n", int(r_), __FILE__, __LINE__, #call); return false; } } while (0)

bool Context::init(void* hwnd, void* hinstance, uint32_t width, uint32_t height, bool) {
    if (!loadLibrary()) return false;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Foundry"; app.pEngineName = "Foundry"; app.apiVersion = VK_API_VERSION_1_3;
    const char* instExt[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app; ici.enabledExtensionCount = 2; ici.ppEnabledExtensionNames = instExt;
    VKCHECK(f_.vkCreateInstance(&ici, nullptr, &inst_));
#define X(n) f_.n = reinterpret_cast<PFN_##n>(f_.vkGetInstanceProcAddr(inst_, #n));
    FOUNDRY_VK_INSTANCE_FUNCS(X)
#undef X

    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    sci.hinstance = static_cast<HINSTANCE>(hinstance); sci.hwnd = static_cast<HWND>(hwnd);
    VKCHECK(f_.vkCreateWin32SurfaceKHR(inst_, &sci, nullptr, &surface_));

    uint32_t n = 0;
    f_.vkEnumeratePhysicalDevices(inst_, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    f_.vkEnumeratePhysicalDevices(inst_, &n, devs.data());
    int bestScore = -1;
    for (auto d : devs) {
        VkPhysicalDeviceProperties p; f_.vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        uint32_t qn = 0; f_.vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        f_.vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qs.data());
        for (uint32_t i = 0; i < qn; ++i) {
            VkBool32 present = VK_FALSE;
            f_.vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
            if (!(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                      : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > bestScore) { bestScore = score; phys_ = d; queueFamily_ = i; }
            break;
        }
    }
    if (!phys_) { std::fprintf(stderr, "No Vulkan 1.3 device with graphics+present found\n"); return false; }
    VkPhysicalDeviceProperties props; f_.vkGetPhysicalDeviceProperties(phys_, &props);
    std::snprintf(deviceName_, sizeof deviceName_, "%s", props.deviceName);
    f_.vkGetPhysicalDeviceMemoryProperties(phys_, &memProps_);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily_; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE; f13.synchronization2 = VK_TRUE;
    const char* devExt[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f13; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = devExt;
    VKCHECK(f_.vkCreateDevice(phys_, &dci, nullptr, &dev_));
#define X(n) f_.n = reinterpret_cast<PFN_##n>(f_.vkGetDeviceProcAddr(dev_, #n));
    FOUNDRY_VK_DEVICE_FUNCS(X)
#undef X
    f_.vkGetDeviceQueue(dev_, queueFamily_, 0, &queue_);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = queueFamily_;
    VKCHECK(f_.vkCreateCommandPool(dev_, &pci, nullptr, &pool_));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = kFramesInFlight;
    VKCHECK(f_.vkAllocateCommandBuffers(dev_, &cai, cmds_));
    for (int i = 0; i < kFramesInFlight; ++i) {
        VkSemaphoreCreateInfo sci2{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VKCHECK(f_.vkCreateSemaphore(dev_, &sci2, nullptr, &acquire_[i]));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VKCHECK(f_.vkCreateFence(dev_, &fci, nullptr, &fence_[i]));
    }
    return createSwapchain(width, height);
}

bool Context::createSwapchain(uint32_t w, uint32_t h) {
    VkSurfaceCapabilitiesKHR caps;
    VKCHECK(f_.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps));
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {
        ext.width = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
        ext.height = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (ext.width == 0 || ext.height == 0) return false;   // minimized

    uint32_t fn = 0;
    f_.vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fn);
    f_.vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &fn, fmts.data());
    VkSurfaceFormatKHR sf = fmts.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : fmts[0];
    for (auto& c : fmts) if (c.format == VK_FORMAT_B8G8R8A8_UNORM) { sf = c; break; }

    uint32_t count = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount) count = std::min(count, caps.maxImageCount);

    VkSwapchainKHR old = swapchain_;
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface_; sci.minImageCount = count;
    sci.imageFormat = sf.format; sci.imageColorSpace = sf.colorSpace; sci.imageExtent = ext;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;   // vsync; always available
    sci.clipped = VK_TRUE; sci.oldSwapchain = old;
    VkSwapchainKHR sc;
    VKCHECK(f_.vkCreateSwapchainKHR(dev_, &sci, nullptr, &sc));
    if (old) { f_.vkDeviceWaitIdle(dev_); destroySwapchain(); f_.vkDestroySwapchainKHR(dev_, old, nullptr); }
    swapchain_ = sc; format_ = sf.format; extent_ = ext;

    uint32_t n = 0;
    f_.vkGetSwapchainImagesKHR(dev_, swapchain_, &n, nullptr);
    images_.resize(n);
    f_.vkGetSwapchainImagesKHR(dev_, swapchain_, &n, images_.data());
    views_.resize(n); renderDone_.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = images_[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VKCHECK(f_.vkCreateImageView(dev_, &vi, nullptr, &views_[i]));
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VKCHECK(f_.vkCreateSemaphore(dev_, &si, nullptr, &renderDone_[i]));
    }
    return true;
}

void Context::destroySwapchain() {
    for (auto v : views_) f_.vkDestroyImageView(dev_, v, nullptr);
    for (auto s : renderDone_) f_.vkDestroySemaphore(dev_, s, nullptr);
    views_.clear(); renderDone_.clear(); images_.clear();
}

bool Context::resize(uint32_t w, uint32_t h) {
    if (!dev_) return false;
    f_.vkDeviceWaitIdle(dev_);
    return createSwapchain(w, h);
}

bool Context::beginFrame(Frame& fr) {
    if (!swapchain_) return false;
    f_.vkWaitForFences(dev_, 1, &fence_[cur_], VK_TRUE, UINT64_MAX);
    uint32_t idx;
    VkResult r = f_.vkAcquireNextImageKHR(dev_, swapchain_, UINT64_MAX, acquire_[cur_], nullptr, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { return false; }   // caller resizes next frame
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
    f_.vkResetFences(dev_, 1, &fence_[cur_]);
    VkCommandBuffer cmd = cmds_[cur_];
    f_.vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    f_.vkBeginCommandBuffer(cmd, &bi);
    barrier(f_, cmd, images_[idx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    fr = {cmd, images_[idx], views_[idx], extent_, idx};
    return true;
}

void Context::endFrame(const Frame& fr) {
    VkCommandBuffer cmd = fr.cmd;
    bool doCapture = capture_;
    if (doCapture) {
        capSize_ = VkDeviceSize(extent_.width) * extent_.height * 4;
        if (capBuf_) { f_.vkDestroyBuffer(dev_, capBuf_, nullptr); f_.vkFreeMemory(dev_, capMem_, nullptr); capBuf_ = nullptr; }
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = capSize_; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkMemoryRequirements req;
        uint32_t mt = 0;
        if (f_.vkCreateBuffer(dev_, &bi, nullptr, &capBuf_) == VK_SUCCESS) {
            f_.vkGetBufferMemoryRequirements(dev_, capBuf_, &req);
            if (findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mt)) {
                VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                mai.allocationSize = req.size; mai.memoryTypeIndex = mt;
                if (f_.vkAllocateMemory(dev_, &mai, nullptr, &capMem_) == VK_SUCCESS)
                    f_.vkBindBufferMemory(dev_, capBuf_, capMem_, 0);
                else doCapture = false;
            } else doCapture = false;
        } else doCapture = false;
    }
    if (doCapture) {
        barrier(f_, cmd, fr.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {extent_.width, extent_.height, 1};
        f_.vkCmdCopyImageToBuffer(cmd, fr.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capBuf_, 1, &c);
        barrier(f_, cmd, fr.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, 0, 0);
    } else {
        barrier(f_, cmd, fr.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0, 0);
    }
    f_.vkEndCommandBuffer(cmd);

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = acquire_[cur_]; wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo sig{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    sig.semaphore = renderDone_[fr.imageIndex]; sig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbi.commandBuffer = cmd;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = 1; si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = 1; si.pCommandBufferInfos = &cbi;
    si.signalSemaphoreInfoCount = 1; si.pSignalSemaphoreInfos = &sig;
    f_.vkQueueSubmit2(queue_, 1, &si, fence_[cur_]);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &renderDone_[fr.imageIndex];
    pi.swapchainCount = 1; pi.pSwapchains = &swapchain_; pi.pImageIndices = &fr.imageIndex;
    f_.vkQueuePresentKHR(queue_, &pi);

    if (doCapture) {
        f_.vkWaitForFences(dev_, 1, &fence_[cur_], VK_TRUE, UINT64_MAX);
        void* p = nullptr;
        if (f_.vkMapMemory(dev_, capMem_, 0, capSize_, 0, &p) == VK_SUCCESS) {
            capPixels_.assign(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + capSize_);
            f_.vkUnmapMemory(dev_, capMem_);
            if (format_ == VK_FORMAT_B8G8R8A8_UNORM || format_ == VK_FORMAT_B8G8R8A8_SRGB)
                for (size_t i = 0; i + 3 < capPixels_.size(); i += 4) std::swap(capPixels_[i], capPixels_[i + 2]);
            capW_ = extent_.width; capH_ = extent_.height; captured_ = true;
        }
        capture_ = false;
    }
    cur_ = (cur_ + 1) % kFramesInFlight;
}

bool Context::takeCapture(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
    if (!captured_) return false;
    rgba = std::move(capPixels_); w = capW_; h = capH_; captured_ = false;
    return true;
}

void Context::shutdown() {
    if (dev_) {
        f_.vkDeviceWaitIdle(dev_);
        if (capBuf_) { f_.vkDestroyBuffer(dev_, capBuf_, nullptr); f_.vkFreeMemory(dev_, capMem_, nullptr); capBuf_ = nullptr; }
        destroySwapchain();
        if (swapchain_) f_.vkDestroySwapchainKHR(dev_, swapchain_, nullptr);
        for (int i = 0; i < kFramesInFlight; ++i) {
            if (acquire_[i]) f_.vkDestroySemaphore(dev_, acquire_[i], nullptr);
            if (fence_[i]) f_.vkDestroyFence(dev_, fence_[i], nullptr);
        }
        if (pool_) f_.vkDestroyCommandPool(dev_, pool_, nullptr);
        f_.vkDestroyDevice(dev_, nullptr);
        dev_ = nullptr; swapchain_ = nullptr;
    }
    if (inst_) {
        if (surface_) f_.vkDestroySurfaceKHR(inst_, surface_, nullptr);
        f_.vkDestroyInstance(inst_, nullptr);
        inst_ = nullptr; surface_ = nullptr;
    }
    if (lib_) { FreeLibrary(static_cast<HMODULE>(lib_)); lib_ = nullptr; }
}

} // namespace foundry::rhi::vk
