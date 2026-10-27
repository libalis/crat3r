#pragma once

#include <volk.h>

#include "core/vulkan_app.hpp"

class ParticleRenderer final {
  public:
    void init(VulkanApp* app);
    void deinit();

    void recordCommandBuffer(VkCommandBuffer commandBuffer, VkBuffer particleBuffer, uint32_t numParticles);
    void onResize();

  private:
    VkResult createRenderPass();
    VkResult createPipelineLayout();
    VkResult createGraphicsPipeline();
    VkResult createGeometryBuffers();
    VkResult createFramebuffer();
    void freeFramebuffer();

    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        VkResult create(VmaAllocator alloc, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags);
        void destroy(VmaAllocator alloc);
    };

  private:
    VulkanApp* m_app;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

    Buffer m_vertexBuffer{};
    Buffer m_indexBuffer{};

    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
};
