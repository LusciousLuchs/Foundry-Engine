#pragma once
// Backend-agnostic render hardware interface. Modeled on Vulkan's explicit
// design (command lists, pipeline objects) so Vulkan maps 1:1 and OpenGL
// emulates it. Game and renderer code must only include this header.
#include <cstdint>
#include <memory>
#include <span>

namespace foundry::rhi {

enum class Backend { Vulkan, OpenGL };

struct Handle { uint32_t id = 0; explicit operator bool() const { return id != 0; } };
struct BufferHandle   : Handle {};
struct TextureHandle  : Handle {};
struct PipelineHandle : Handle {};

enum class BufferUsage : uint32_t { Vertex = 1, Index = 2, Uniform = 4, Storage = 8, Indirect = 16 };

struct BufferDesc   { uint64_t size = 0; BufferUsage usage = BufferUsage::Vertex; bool cpuVisible = false; };
struct TextureDesc  { uint32_t width = 1, height = 1, mips = 1; uint32_t format = 0; bool renderTarget = false; };
struct PipelineDesc { const char* vertexSpv = nullptr; const char* fragmentSpv = nullptr; };

class CommandList {
public:
    virtual ~CommandList() = default;
    virtual void bindPipeline(PipelineHandle) = 0;
    virtual void bindVertexBuffer(BufferHandle, uint64_t offset = 0) = 0;
    virtual void bindIndexBuffer(BufferHandle, uint64_t offset = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1) = 0;
};

class Device {
public:
    virtual ~Device() = default;
    virtual Backend backend() const = 0;
    virtual BufferHandle   createBuffer(const BufferDesc&) = 0;
    virtual TextureHandle  createTexture(const TextureDesc&) = 0;
    virtual PipelineHandle createPipeline(const PipelineDesc&) = 0;
    virtual void upload(BufferHandle, std::span<const std::byte> data, uint64_t offset = 0) = 0;
    virtual CommandList& beginFrame() = 0;
    virtual void endFrame() = 0;
};

// Implemented per backend (rhi/vulkan, rhi/opengl).
std::unique_ptr<Device> createDevice(Backend backend, void* nativeWindow);

} // namespace foundry::rhi
