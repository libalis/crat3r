#include <volk.h>
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include "core/check_error.hpp"
#include "core/filesystem.hpp"
#include "core/vulkan_app.hpp"
#include "particle.h"
#include "particle_renderer.hpp"

#include <array>
#include <cstring>
#include <random>
#include <vector>

//--------------------------------------------------------------------------------------------------

constexpr uint32_t WIDTH = 1920;
constexpr uint32_t HEIGHT = 1080;
constexpr bool VSYNC = true;

constexpr uint32_t NUM_PARTICLES = 0x10000; // 65536

//--------------------------------------------------------------------------------------------------

struct UniformBufferObject {
    glm::vec4 spawn; // xyz = position, w = radius

    float lifetimeMin;
    float lifetimeMax;

    float forceFieldSize;
    float forceFieldStrength;
    float dragStrength;

    float time;
    float dt;

    uint32_t numParticles;
};

//--------------------------------------------------------------------------------------------------

class Assignment final {
  public:
    void init() {
        VK_CHECK(m_app.init("crat3r", WIDTH, HEIGHT, VSYNC));
        m_app.setCameraAngles(0.2, 2.3, 5.0);
        m_particleRenderer.init(&m_app);

        m_ubo.spawn = glm::vec4(0.f, 0.f, 0.f, 1.f);
        m_ubo.lifetimeMin = 1.f;
        m_ubo.lifetimeMax = 3.f;
        m_ubo.forceFieldSize = 2.f;
        m_ubo.forceFieldStrength = 5.f;
        m_ubo.dragStrength = 1.f;
        m_ubo.time = 0.f;
        m_ubo.dt = 0.f;
        m_ubo.numParticles = NUM_PARTICLES;

        createStorageBuffer();

        createUniformBuffers();

        createForceFieldImage();
        createForceFieldImageView();
        createForceFieldImageSampler();

        createDescriptorPool();
        createDescriptorSetLayout();
        createDescriptorSets();

        createPipelineLayout();
        createPipeline();
    }

    void run() {
        m_app.run(
            [&]() { // onRenderUi
                buildUi();
            },
            [&](VkCommandBuffer commandBuffer) { // onRender
                recordCommandBuffer(commandBuffer);
                m_particleRenderer.recordCommandBuffer(commandBuffer, m_storageBuffer, NUM_PARTICLES);
            },
            [&]() { // onResize
                m_particleRenderer.onResize();
            }
        );
    }

    void deinit() {
        const VkDevice device = m_app.getDevice();

        // TODO: Assignment 1.x
        // Destroy and free any ressources you have created.

        m_particleRenderer.deinit();
        m_app.deinit();
    }

  private:
    VulkanApp m_app{};
    ParticleRenderer m_particleRenderer{};

    VkBuffer m_storageBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_storageBufferMemory = VK_NULL_HANDLE;

    // TODO: Assignment 1.x
    // Add any other necessary class members here.

    UniformBufferObject m_ubo{};

  private:
    //--------------------------------------------------------------------------------------------------

    void createStorageBuffer() {
        std::vector<Particle> particlesInit(NUM_PARTICLES);

        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_real_distribution<float> uniform{};

        for (uint32_t i = 0; i < NUM_PARTICLES; i++) {
            const glm::vec3 pos = glm::vec3(uniform(rng), uniform(rng), uniform(rng)) * 2.f - 1.f;
            const glm::vec3 vel = glm::vec3(uniform(rng), uniform(rng), uniform(rng)) * 2.f - 1.f;
            const float lifetime = m_ubo.lifetimeMin + uniform(rng) * (m_ubo.lifetimeMax - m_ubo.lifetimeMin);
            const float life = uniform(rng) * 0.8f + 0.1f;
            particlesInit[i].pos = glm::vec4(pos, lifetime);
            particlesInit[i].vel = glm::vec4(vel * 0.2f, life);
        }

        const VkDeviceSize bufferSize = sizeof(Particle) * NUM_PARTICLES;

        // TODO: Assignment 1.1
        // First, implement the 'createBuffer' function later in this file. Then use it to allocate a storage buffer
        // for the particles. It will be used as both a storage buffer for a compute shader and as a vertex buffer
        // for rendering. It must be host-visible and host-coherent, because we are going to upload the initial data to
        // it. Pass the size and the appropriate flags together with 'm_storageBuffer' / 'm_storageBufferMemory' to
        // 'createBuffer'. Do not forget to free the buffer and memory in deinit()! Afterwards, map the buffer to
        // CPU-memory and copy the initial data to it. You may then unmap the memory immediately after writing to it.
        //
        // [ New functions: vkMapMemory, vkUnmapMemory ]

        // TODO: Assignment 1.2
        // Now, create a staging buffer so we can store the particle data in device local memory.
        // First, have the 'createBuffer' invocation above write to local 'VkBuffer' / 'VkDeviceMemory' handles,
        // and update the usage of the staging buffer to 'transfer source'.
        // Next, create a second buffer that will now be used as the storage / vertex buffer, but also as a transfer
        // destination. This time allocate it in device local memory and write its handles to 'm_storageBuffer' and
        // 'm_storageBufferMemory'. Finally, copy the staging buffer to the storage buffer.
        //
        // [ New functions: vkCmdCopyBuffer ]
        //
        // Hint: To execute the copy command a command buffer is required, You may use:
        // VK_CHECK(m_app.executeSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        //    ...
        // }));
    }

    void createUniformBuffers() {
        const uint32_t framesInFlight = m_app.getSwapchain().getMaxFramesInFlight();

        // TODO: Assignment 1.5
        // First, add storage for 'framesInFlight' uniform buffers as class members. Each buffer requires a
        // 'VkBuffer', a 'VkDeviceMemory' and a mapped 'void*'. Either create three different vectors, or create
        // a single vector of structs. Then create the buffers similar to the particle staging buffer and
        // map them to CPU-memory. The size of the buffers is 'sizeof(UniformBufferObject)'.
    }

    void createForceFieldImage() {
        const std::vector<char> vectorfield = Fs::readFile("assets/vectorfield.raw");
        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(vectorfield.size());
        const VkExtent3D imageExtent = {.width = 4, .height = 4, .depth = 4};
        const VkFormat imageFormat = VK_FORMAT_R8G8B8A8_UNORM;

        assert(imageSize == imageExtent.width * imageExtent.height * imageExtent.depth * 4);

        // TODO: Assignment 1.7
        // First, create a staging buffer and upload the vector field data to it. Then create a device-local 3D image
        // to be sampled by the compute shader. Implement the 'createImage' for this later in this file. The image
        // usage flags should the image to be used as a transfer destination and by a sampler in the shader. The tiling
        // can be set to optimal. Next, execute a single time command buffer. First, transition the image layout from
        // undefined to transfer destination. You may use the already implemented function 'transitionImageLayout'.
        // Then, copy the data from the buffer to the image. In 'VkBufferImageCopy' you can set most fields to zero,
        // except the 'imageExtent' and 'imageSubresource'. Finally, transition the image layout again for optimal
        // shader reading. Remember to free the staging buffer!
        //
        // [ New functions: vkCmdCopyBufferToImage ]
    }

    void createForceFieldImageView() {
        const VkFormat imageFormat = VK_FORMAT_R8G8B8A8_UNORM;

        // TODO: Assignment 1.7
        // Create a 3D image view of the force field image. Remember to set the both 'layerCount' and
        // 'levelCount' fields of the 'subresourceRange' in the create info to one.
        //
        // [ New functions: vkCreateImageView ]
    }

    void createForceFieldImageSampler() {
        // TODO: Assignment 1.7
        // Create a sampler that linearly samples the force field image. Apart from the minification and magnification
        // settings, you may set most of the other fields of the sampler create info to the default values.
        //
        // [ New functions: vkCreateSampler ]
    }

    void createDescriptorPool() {
        const uint32_t framesInFlight = m_app.getSwapchain().getMaxFramesInFlight();

        // TODO: Assignment 1.3
        // Fill a single 'VkDescriptorPoolSize' struct for 'framesInFlight' storage buffer descriptors.
        // Then create a descriptor pool with capacity for 'framesInFlight' descriptor sets.
        // Store the 'VkDescriptorPool' in a new class member and don't forget to destroy it in deinit.
        //
        // [ New functions: vkCreateDescriptorPool ]

        // TODO: Assignment 1.5
        // Now add space for 'framesInFlight' uniform buffers. Fill a second 'VkDescriptorPoolSize' and
        // store both in a vector or array. Then update 'VkDescriptorPoolCreateInfo' accordingly.

        // TODO: Assignment 1.7
        // Now also add space for 'framesInFlight' combined image samplers for the force field image.
    }

    void createDescriptorSetLayout() {
        // TODO: Assignment 1.3
        // Fill a 'VkDescriptorSetLayoutBinding' for one storage buffer at binding 0 in the compute stage.
        // Then, create a descriptor set layout for that single binding.
        //
        // [ New functions: vkCreateDescriptorSetLayout ]

        // TODO: Assignment 1.5
        // Add a second layout binding at location 1 for a uniform buffer, store both bindings in an array
        // or vector and update 'VkDescriptorSetLayoutCreateInfo'.

        // TODO: Assignment 1.7
        // Add a third binding for a combined image sampler at binding location 2.
    }

    void createDescriptorSets() {
        const uint32_t framesInFlight = m_app.getSwapchain().getMaxFramesInFlight();

        // TODO: Assignment 1.3
        // First, add a vector of descriptor sets as a class member and resize it here to hold 'framesInFlight' items.
        // Then, create a local vector that contains the descriptor set layout 'framesInFlight' times.
        // Finally, allocate the 'framesInFlight' descriptor sets.
        //
        // [ New functions: vkAllocateDescriptorSets ]

        for (size_t i = 0; i < framesInFlight; i++) {
            // TODO: Assignment 1.3
            // Now, update the descriptor sets to reference the storage buffer. Fill 'VkDescriptorBufferInfo' so
            // that it points to the buffer. Then fill 'VkWriteDescriptorSet' to point to binding 0 of the ith set.
            // Reference the buffer info and set the descriptor type. Lastly, update the descriptor set.
            //
            // [ New functions: 'vkUpdateDescriptorSets' ]

            // TODO: Assignment 1.5
            // Add a second 'VkDescriptorBufferInfo' for the uniform buffer and have it reference the ith buffer.
            // Then, add a second 'VkWriteDescriptorSet' for the uniform buffer, storing both in an array or vector
            //
            // and update 'vkUpdateDescriptorSets'.

            // TODO: Assignment 1.7
            // Add a 'VkDescriptorImageInfo' and have it reference the image view and sampler.
            // Then, add a third 'VkWriteDescriptorSet' for the image.
        }
    }

    void createPipelineLayout() {
        // TODO: Assignment 1.4
        // Create a pipeline layout with the previously created descriptor set layout.
        //
        // [ New functions: vkCreatePipelineLayout ]
    }

    void createPipeline() {
        const std::vector<char> code = Fs::readFile("shaders/a01/particles.comp.spv");

        // TODO: Assignment 1.4
        // First, create a shader module from the shader code. You may store it locally and destroy it at the
        // end of this function. Next, create the compute pipeline itself. Note that the entry function name in
        // 'VkPipelineShaderStageCreateInfo' is "main" and the pipeline cache can be 'VK_NULL_HANDLE'.
        //
        // [ New functions: vkCreateShaderModule, vkDestroyShaderModule, vkCreateComputePipelines ]
    }

    //--------------------------------------------------------------------------------------------------

    void recordCommandBuffer(VkCommandBuffer commandBuffer) {
        const uint32_t currentFrame = m_app.getSwapchain().getCurrentFrameIdx();
        updateUniformBuffer(currentFrame);

        // TODO: Assignment 1.4
        // First, bind the compute pipeline. Then, bind the descriptor set for the 'currentFrame'.
        // Finally, dispatch the compute shader. The shader is configured to have a group size of 256,
        // so the group counts are x = div_round_up(NUM_PARTICLES, 256), y = 1, z = 1.
        //
        // [ New functions: vkCmdBindPipeline, vkCmdBindDescriptorSets, vkCmdDispatch ]
    }

    void updateUniformBuffer(uint32_t currentFrame) {
        m_ubo.dt = m_app.getDeltaTime();
        m_ubo.time += m_ubo.dt;

        // TODO: Assignment 1.5
        // Copy 'm_ubo' to the mapped uniform buffer of the current frame.
    }

    //--------------------------------------------------------------------------------------------------

    uint32_t findMemoryType(uint32_t memoryTypeBits, VkMemoryPropertyFlags propertyFlags) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_app.getPhysicalDevice(), &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((memoryTypeBits & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags) {
                return i;
            }
        }

        VK_CHECK(VK_ERROR_UNKNOWN);
    }

    void createBuffer(
        VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags propertyFlags, VkBuffer& buffer,
        VkDeviceMemory& memory
    ) {
        // TODO: Assignment 1.1
        // First, create a new Vulkan buffer of the specified size and usage. Then query the memory requirements, find
        // a suitable memory type, and allocate the buffer's memory. Note that the size given by the memory
        // requirements and the size passed to this function may differ. Finally, bind the memory to the buffer.
        //
        // [ New functions: vkCreateBuffer, vkGetBufferMemoryRequirements, findMemoryType, vkAllocateMemory,
        // vkBindBufferMemory ]
        //
        // Hint: You can check Vulkan calls for errors using the macro 'VK_CHECK(...)'.
        //
        // Hint: The VkAllocationCallbacks* argument in all Vulkan calls can be nullptr.

        const VkDevice device = m_app.getDevice();
    }

    void createImage(
        VkImageType imageType, VkFormat format, VkExtent3D extent, VkImageTiling tiling, VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory
    ) {
        // TODO: Assignment 1.7
        // Implement analogously to 'createBuffer'. As we will not be implementing mip-mapping here, set the
        // number of mip levels to one. Multisampling is also not required. The initial layout of the image
        // should be 'VK_IMAGE_LAYOUT_UNDEFINED'.
        //
        // [ New functions: vkCreateImage, vkGetImageMemoryRequirements, vkBindImageMemory ]
    }

    void transitionImageLayout(
        VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout
    ) {
        VkAccessFlags srcAccessMask;
        VkAccessFlags dstAccessMask;
        VkPipelineStageFlags srcStage;
        VkPipelineStageFlags dstStage;

        switch (oldLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            srcAccessMask = VK_ACCESS_NONE;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;
        default:
            fprintf(stderr, "transitionImageLayout: unsupported old layout!");
            VK_CHECK(VK_ERROR_UNKNOWN);
        }

        switch (newLayout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        case VK_IMAGE_LAYOUT_GENERAL:
            dstAccessMask = VK_ACCESS_NONE;
            dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            break;
        default:
            fprintf(stderr, "transitionImageLayout: unsupported new layout!");
            VK_CHECK(VK_ERROR_UNKNOWN);
        }

        const VkImageSubresourceRange subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = srcAccessMask,
            .dstAccessMask = dstAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = subresourceRange,
        };

        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    //--------------------------------------------------------------------------------------------------

    void buildUi() {
        ImGui::Text("Spawn");
        ImGui::Separator();
        ImGui::DragFloat3("origin", &m_ubo.spawn.x, 0.01f, 0.f, 0.f, "%.2f");
        ImGui::DragFloat("radius", &m_ubo.spawn.w, 0.01f, 0.f, 10.f, "%.2f");

        ImGui::Spacing();

        ImGui::Text("Lifetime");
        ImGui::Separator();
        ImGui::DragFloat("min", &m_ubo.lifetimeMin, 0.01f, 0.f, m_ubo.lifetimeMax, "%.2f");
        ImGui::DragFloat("max", &m_ubo.lifetimeMax, 0.01f, m_ubo.lifetimeMin, 100.f, "%.2f");

        ImGui::Spacing();

        ImGui::Text("Forces");
        ImGui::Separator();
        ImGui::DragFloat("fld sz", &m_ubo.forceFieldSize, 0.1f, 10.f, 0.f, "%.2f");
        ImGui::DragFloat("field", &m_ubo.forceFieldStrength, 0.01f, 0.f, 0.f, "%.2f");
        ImGui::DragFloat("drag", &m_ubo.dragStrength, 0.01f, 0.f, 0.f, "%.2f");
    }
};

//--------------------------------------------------------------------------------------------------

int main() {
    Assignment app;
    app.init();
    app.run();
    app.deinit();

    return EXIT_SUCCESS;
}
