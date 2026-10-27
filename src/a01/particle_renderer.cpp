#include "particle_renderer.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/check_error.hpp"
#include "core/filesystem.hpp"
#include "particle.h"

#include <array>
#include <cstring>

//--------------------------------------------------------------------------------------------------

struct RenderPushConstants {
    glm::mat4 modelview;
    glm::mat4 projection;
};

struct Vertex {
    glm::vec2 pos;
    glm::vec2 uv;
};

const std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f}, {0.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

//--------------------------------------------------------------------------------------------------

void ParticleRenderer::init(VulkanApp* app) {
    m_app = app;

    VK_CHECK(createRenderPass());
    VK_CHECK(createPipelineLayout());
    VK_CHECK(createGraphicsPipeline());
    VK_CHECK(createGeometryBuffers());
    VK_CHECK(createFramebuffer());
}

void ParticleRenderer::deinit() {
    const VkDevice device = m_app->getDevice();
    const VmaAllocator allocator = m_app->getAllocator();

    freeFramebuffer();

    m_vertexBuffer.destroy(allocator);
    m_indexBuffer.destroy(allocator);

    vkDestroyPipeline(device, m_graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyRenderPass(device, m_renderPass, nullptr);
}

void ParticleRenderer::onResize() {
    freeFramebuffer();
    VK_CHECK(createFramebuffer());
}

//--------------------------------------------------------------------------------------------------

void ParticleRenderer::recordCommandBuffer(
    VkCommandBuffer commandBuffer, VkBuffer particleBuffer, uint32_t numParticles
) {
    const VkExtent2D extent = m_app->getRenderTargetExtent();
    const VkClearValue clearColor = {{{0.0f, (particleBuffer != VK_NULL_HANDLE ? 0.0f : 1.0f), 0.0f, 1.0f}}};

    const VkRenderPassBeginInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = m_renderPass,
        .framebuffer = m_framebuffer,
        .renderArea{.offset = {0, 0}, .extent = extent},
        .clearValueCount = 1,
        .pClearValues = &clearColor,
    };

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    const VkRect2D scissor{
        .offset = {0, 0},
        .extent = extent,
    };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    const RenderPushConstants pc{
        .modelview = m_app->getViewMat(),
        .projection = m_app->getProjMat(),
    };
    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    if (particleBuffer != VK_NULL_HANDLE) {
        const std::array offsets = {VkDeviceSize(0), VkDeviceSize(0)};
        const std::array vertexBuffers = {m_vertexBuffer.buffer, particleBuffer};
        vkCmdBindVertexBuffers(commandBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets.data());

        vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdDrawIndexed(commandBuffer, indices.size(), numParticles, 0, 0, 0);
    }

    vkCmdEndRenderPass(commandBuffer);
}

//--------------------------------------------------------------------------------------------------

VkResult ParticleRenderer::createRenderPass() {
    const VkAttachmentDescription colorAttachment{
        .format = m_app->getRenderTargetFormat(),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    const VkAttachmentReference colorAttachmentRef{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    const VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
    };

    const VkSubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    const VkRenderPassCreateInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    VK_CHECK_RETURN(vkCreateRenderPass(m_app->getDevice(), &renderPassInfo, nullptr, &m_renderPass));

    return VK_SUCCESS;
}

VkResult ParticleRenderer::createPipelineLayout() {
    const VkPushConstantRange pushConstant{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(RenderPushConstants),
    };

    const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstant,
    };

    VK_CHECK_RETURN(vkCreatePipelineLayout(m_app->getDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout));

    return VK_SUCCESS;
}

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    const VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data()),
    };

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));

    return shaderModule;
}

VkResult ParticleRenderer::createGraphicsPipeline() {
    auto vertShaderCode = Fs::readFile("shaders/a01/particle.vert.spv");
    auto fragShaderCode = Fs::readFile("shaders/a01/particle.frag.spv");

    const VkShaderModule vertShaderModule = createShaderModule(m_app->getDevice(), vertShaderCode);
    const VkShaderModule fragShaderModule = createShaderModule(m_app->getDevice(), fragShaderCode);

    const VkPipelineShaderStageCreateInfo vertShaderStageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertShaderModule,
        .pName = "main",
    };

    const VkPipelineShaderStageCreateInfo fragShaderStageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragShaderModule,
        .pName = "main",
    };

    const std::array shaderStages = {
        vertShaderStageInfo,
        fragShaderStageInfo,
    };

    const std::array bindingDescriptions{
        VkVertexInputBindingDescription{
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        VkVertexInputBindingDescription{
            .binding = 1,
            .stride = sizeof(Particle),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        }
    };

    const std::array attributeDescriptions{
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(Vertex, pos),
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(Vertex, uv),
        },
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 1,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Particle, pos),
        },
        VkVertexInputAttributeDescription{
            .location = 3,
            .binding = 1,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Particle, vel),
        },
        VkVertexInputAttributeDescription{
            .location = 4,
            .binding = 1,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = offsetof(Particle, vel.w),
        }
    };

    const VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = bindingDescriptions.size(),
        .pVertexBindingDescriptions = bindingDescriptions.data(),
        .vertexAttributeDescriptionCount = attributeDescriptions.size(),
        .pVertexAttributeDescriptions = attributeDescriptions.data(),
    };

    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    const VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    const VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    const VkPipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    const VkPipelineDepthStencilStateCreateInfo depthStencilState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
        .back = {.compareOp = VK_COMPARE_OP_ALWAYS},
    };

    const VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    const std::array dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamicStates.size(),
        .pDynamicStates = dynamicStates.data(),
    };

    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = shaderStages.size(),
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencilState,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = m_pipelineLayout,
        .renderPass = m_renderPass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
    };

    VK_CHECK_RETURN(
        vkCreateGraphicsPipelines(m_app->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline)
    );

    vkDestroyShaderModule(m_app->getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(m_app->getDevice(), vertShaderModule, nullptr);

    return VK_SUCCESS;
}

VkResult ParticleRenderer::createGeometryBuffers() {
    const VmaAllocator allocator = m_app->getAllocator();

    // vertex buffer
    {
        const VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

        Buffer stagingBuffer;
        VK_CHECK_RETURN(stagingBuffer.create(
            allocator,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));

        void* data;
        VK_CHECK_RETURN(vmaMapMemory(allocator, stagingBuffer.allocation, &data));
        std::memcpy(data, vertices.data(), bufferSize);
        vmaUnmapMemory(allocator, stagingBuffer.allocation);

        VK_CHECK_RETURN(m_vertexBuffer.create(
            allocator,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        ));

        VK_CHECK_RETURN(m_app->executeSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            VkBufferCopy copyRegion{};
            copyRegion.size = bufferSize;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, m_vertexBuffer.buffer, 1, &copyRegion);
        }));

        stagingBuffer.destroy(allocator);
    }

    // index buffer
    {
        const VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        Buffer stagingBuffer;
        VK_CHECK_RETURN(stagingBuffer.create(
            allocator,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));

        void* data;
        VK_CHECK_RETURN(vmaMapMemory(allocator, stagingBuffer.allocation, &data));
        std::memcpy(data, indices.data(), bufferSize);
        vmaUnmapMemory(allocator, stagingBuffer.allocation);

        VK_CHECK_RETURN(m_indexBuffer.create(
            allocator,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        ));

        VK_CHECK_RETURN(m_app->executeSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            VkBufferCopy copyRegion{};
            copyRegion.size = bufferSize;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, m_indexBuffer.buffer, 1, &copyRegion);
        }));

        stagingBuffer.destroy(allocator);
    }

    return VK_SUCCESS;
}

VkResult ParticleRenderer::createFramebuffer() {
    const VkImageView imageAttachment = m_app->getRenderTargetImageView();

    const VkFramebufferCreateInfo framebufferInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = m_renderPass,
        .attachmentCount = 1,
        .pAttachments = &imageAttachment,
        .width = m_app->getRenderTargetExtent().width,
        .height = m_app->getRenderTargetExtent().height,
        .layers = 1,
    };

    VK_CHECK_RETURN(vkCreateFramebuffer(m_app->getDevice(), &framebufferInfo, nullptr, &m_framebuffer));

    return VK_SUCCESS;
}

void ParticleRenderer::freeFramebuffer() {
    vkDestroyFramebuffer(m_app->getDevice(), m_framebuffer, nullptr);
}

//--------------------------------------------------------------------------------------------------

VkResult ParticleRenderer::Buffer::create(
    VmaAllocator alloc, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags
) {
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateFlags allocFlags{};
    if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        allocFlags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    const VmaAllocationCreateInfo allocInfo{
        .flags = allocFlags,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = flags,
    };

    return vmaCreateBuffer(alloc, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
}

void ParticleRenderer::Buffer::destroy(VmaAllocator alloc) {
    vmaDestroyBuffer(alloc, buffer, allocation);
}
