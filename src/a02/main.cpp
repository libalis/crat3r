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

#include "common/buffer.hpp"
#include "common/image.hpp"
#include "common/scene.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

//--------------------------------------------------------------------------------------------------

constexpr uint32_t WIDTH = 1920;
constexpr uint32_t HEIGHT = 1080;
constexpr bool VSYNC = true;

// TODO: Assignment 2.6 / 2.9
// Update the model to be loaded here.
const std::string SCENE = //
    ""
    // "assets/shiba.glb"
    // "assets/tokyo.glb"
    ;

//--------------------------------------------------------------------------------------------------

struct VertexPushConstants {
    glm::mat4 MVP;
};

struct MaterialUniformBuffer {
    glm::vec4 baseColor;
    int32_t hasTexture;
};

//--------------------------------------------------------------------------------------------------

class Assignment final {
  public:
    void init() {
        VK_CHECK(m_app.init("Crat3r", WIDTH, HEIGHT, VSYNC));

        if (!SCENE.empty()) {
            if (!m_scene.load(SCENE)) {
                fprintf(stderr, "Loading of scene failed!");
                VK_CHECK(VK_ERROR_UNKNOWN);
            }

            m_app.setSceneSize(m_scene.sizeWorld);
            m_app.setCameraAngles(0.47f, 4.08f, m_scene.sizeWorld);
        } else {
            m_app.setCameraAngles(0.57f, 0.96f, 2.f);
        }

        createRenderPass();
        createDescriptorSetLayout();
        createPipelineLayout();
        createPipeline();

        createDepthResources();
        createFramebuffer();

        createVertexBuffers();
        createIndexBuffers();

        createTextures();
        createTextureSampler();

        createUniformBuffers();

        createDescriptorPool();
        createDescriptorSets();
    }

    void run() {
        m_app.run(
            [&]() { // onRenderUi
                buildUi();
            },
            [&](VkCommandBuffer commandBuffer) { // onRender
                m_scene.updateAnimationState(m_app.getDeltaTime());
                recordCommandBuffer(commandBuffer);
            },
            [&]() { // onResize
                freeDepthResources();
                freeFramebuffer();

                createDepthResources();
                createFramebuffer();
            }
        );
    }

    void deinit() {
        const VkDevice device = m_app.getDevice();
        const VmaAllocator allocator = m_app.getAllocator();

        // TODO: Assignment 2.x
        // Destroy and free any ressources you have created.

        m_app.deinit();
    }

  private:
    VulkanApp m_app;
    Scene m_scene;

    // TODO: Assignment 2.x
    // Add any other necessary class members here.

  private:
    //--------------------------------------------------------------------------------------------------

    void createRenderPass() {
        const VkFormat targetFormat = m_app.getRenderTargetFormat();

        // TODO: Assignment 2.1
        // First, create a render pass for our rasterizer. This creation info struct references a few other
        // descriptions.
        //
        // Start with a 'VkAttachmentDescription' for the color attachment. The format of the image that we are
        // going to render to is specified by 'targetFormat'. Importantly, set the load operation to CLEAR and the
        // store operation to STORE. We don't care about the stencil operations for this attachment. Set the initial
        // and final layouts to the correct values for the image before and after the render pass.
        //
        // Next, we will create a single subpass for our rendering, that uses the color attachment.
        // So, create a 'VkAttachmentReference' for the first attachment in the optimal layout for color attachment.
        // Then, initalize a graphics subpass with 'VkSubpassDescription' using the attachment reference.
        //
        // To ensure that the image is ready to be written to before the render pass starts, we will create a
        // subpass dependency. In this case, the dependency will be between the previous and current render passes,
        // rather than between subpasses. Fill a 'VkSubpassDependency' with a source subpass of 'VK_SUBPASS_EXTERNAL'.
        // The destination is the first subpass. Set both the 'dstStageMask' and 'dstAccessMask' so that the
        // image can be written as a color attachment. There are multiple options for the source masks; find one
        // that works. Tip: Before rendering, the image is read by a shader in the previous rendering iteration.
        //
        // Finally, create the render pass with the color attachment, the subpass and dependency.
        //
        // [ New functions: vkCreateRenderPass ]

        // TODO: Assignment 2.7
        // Add a second attachment description and attachment reference and subpass dependency
        // for the depth attachment.
    }

    void createDescriptorSetLayout() {
        // TODO: Assignment 2.8
        // Create a descriptor set layout for a single combined image sampler at binding zero.

        // TODO: Assignment 2.12
        // Add a second layout binding for a uniform buffer.
    }

    void createPipelineLayout() {
        // TODO: Assignment 2.1
        // Create a simple pipeline layout for zero descriptor sets and push constant ranges.
        //
        // [ New functions: vkCreatePipelineLayout ]

        // TODO: Assignment 2.4
        // Add a push constant range for the vertex shader using the 'VertexPushConstants' struct.

        // TODO: Assignment 2.8
        // Add the descriptor set layout to the pipeline layout.
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

    void createPipeline() {
        const auto vertShaderCode = Fs::readFile("shaders/a02/simple.vert.spv");
        const auto fragShaderCode = Fs::readFile("shaders/a02/simple.frag.spv");

        const VkShaderModule vertShaderModule = createShaderModule(m_app.getDevice(), vertShaderCode);
        const VkShaderModule fragShaderModule = createShaderModule(m_app.getDevice(), fragShaderCode);

        // TODO: Assignment 2.1
        // Now it is time to create the graphics pipeline. The 'VkGraphicsPipelineCreateInfo' references several
        // other create infos. Refer to the documentation for each create info to understand its purpose.
        // Here are a few tips to help you fill them out:
        // - We need two stages: the vertex and fragment shaders. The entry function name for both is "main".
        // - There is no vertex input for now, so the binding and attribute descriptions can be set to zero.
        // - The primitives should be assembled into a list of triangles.
        // - As we are not using tesselation, the corresponding state can be a nullptr.
        // - The viewports and scissors can be baked into the pipeline. However, to enable resizing of the
        //   surface without rebuilding the pipeline, they can be set dynamically using dynamic states. To do this,
        //   first set the viewport and scissor pointers in the viewport create info to nullptr, while setting
        //   their counts to one. Then, in the dynamic state info, enable the two corresponding dynamic states.
        // - The polygons should be drawn as filled. Disable all other rasterization options for now.
        // - Even though we are not using multisampling, the corresponding state still has to be specified.
        //   Set the sample count to one and disable sample shading to turn off multisampling.
        // - You have to specify color blending for the one color attachment. Blending can be disabled for now;
        //   however, do not forget to enable all color components in the write mask or you will not see anything!
        // If you fill all structs correctly, the pipeline creation should succeed without any complaints from
        // the validation layers. The pipeline cache can be set to 'VK_NULL_HANDLE' again.
        //
        // [ New functions: vkCreateGraphicsPipelines ]

        // TODO: Assignment 2.2
        // Update the vertex input state to enable the upload of vertex data to the rasterizer. We will use one
        // vertex binding, which is described on the CPU by 'Scene::Vertex'. One of these vertices contains two
        // attributes: a three-dimensional position and a two-dimensional UV coordiate. Both of these use 32-bit
        // signed floating-point numbers. You can use 'offsetof(Scene::Vertex, pos)' to find the offset of a
        // field in a struct.

        // TODO: Assignment 2.5
        // Enable back-face culling in the rasterization state. Note that here, by convention, the front faces
        // are oriented counter-clockwise.

        // TODO: Assignment 2.7
        // Add a depth and stencil state and enable the writing of depth values and depth test. Also specify
        // the depth compare operation. The stencil operations should be disabled.

        // TODO: Assignment 2.11
        // Enable color blending for this pipeline. Set the blend operations and factors for standard
        // alpha blending.

        vkDestroyShaderModule(m_app.getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(m_app.getDevice(), vertShaderModule, nullptr);
    }

    void createDepthResources() {
        const VkFormat format = VK_FORMAT_D32_SFLOAT;
        const VkExtent2D extent = m_app.getRenderTargetExtent();

        // TODO: Assignment 2.7
        // Create a device-local image of the specified format and size to be used as a depth attachment.
        // See 'image.hpp' for a convenient helper struct. Then create an image view of the depth image.
    }

    void freeDepthResources() {
        // TODO: Assignment 2.7
        // Destroy the depth image view and free the depth image.
    }

    void createFramebuffer() {
        const VkImageView colorAttachment = m_app.getRenderTargetImageView();
        const VkExtent2D imageExtent = m_app.getRenderTargetExtent();

        // TODO: Assignment 2.1
        // Create a framebuffer that contains the target color image view as a single attachment.
        //
        // [ New functions: vkCreateFramebuffer ]

        // TODO: Assignment 2.7
        // Add the depth image view as a second attachment to the framebuffer.
    }

    void freeFramebuffer() {
        // TODO: Assignment 2.1
        // Free the framebuffer. This function is called when the screen size changes.
        //
        // [ New functions: vkDestroyFramebuffer ]
    }

    void createVertexBuffers() {
        // TODO: Assignment 2.2
        // Upload these vertices into a vertex buffer.
        // Take a look at the utility function 'uploadBufferWithStaging' and the 'buffer.hpp' file.

        // TODO: Assignment 2.6
        // Upload the vertices of the first mesh in the scene instead.

        // TODO: Assignment 2.9
        // Iterate over the meshes (not nodes!) of the scene and create a vertex buffer for each mesh.

        const std::vector<Scene::Vertex> vertices{
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},
            {{0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}},
            {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}},

            {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},
            {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}},
            {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}},
        };
    }

    void createIndexBuffers() {
        // TODO: Assignment 2.3
        // Create an index buffer that holds six indices pointing to the above vertices. Use a 32-bit unsigned integer
        // for the indices. You should then be able to reduce the number of vertices in the vertex buffer to four.

        // TODO: Assignment 2.6
        // Upload the indices of the first mesh in the scene instead.

        // TODO: Assignment 2.9
        // Iterate over the meshes (not nodes!) of the scene and create an index buffer for each mesh.
    }

    void createTextures() {
        const VmaAllocator allocator = m_app.getAllocator();

        // TODO: Assignment 2.8
        // Upload the first texture of the scene ('m_scene.textures[0]') to a device local image. The format of
        // the texture is given by 'texture.format'. You may use the utility function 'uploadImageWithStaging'.
        // Then create an image view for the texture image.

        // TODO: Assignment 2.10
        // Now, create an image and view for each texture used in the scene.
    }

    void createTextureSampler() {
        // TODO: Assignment 2.8
        // Create a sampler for textures with linear minification and magnification.
    }

    void createUniformBuffers() {
        // TODO: Assignment 2.12
        // Upload an instance of 'MaterialUniformBuffer' for each material to the GPU. As the material properties
        // do not change, these buffers can be stored in device local memory. The base color of a material is
        // made up of 'material.colDiffuse' and 'material.opacity'. A material has a texture if
        // 'material.texDiffuseIdx.has_value()' is true.
    }

    void createDescriptorPool() {
        // TODO: Assignment 2.8
        // Create a descriptor pool for one combined image sampler.

        // TODO: Assignment 2.10
        // Reserve enough space for as many descriptors and sets as there are materials (not textures!) in the scene.

        // TODO: Assignment 2.12
        // Additionally reserve enough space for one uniform buffer per material.
    }

    void createDescriptorSets() {
        // TODO: Assignment 2.8
        // Allocate a descriptor set and update it by writing the texture image and sampler to binding zero.

        // TODO: Assignment 2.10
        // Create a seperate descriptor set for each material. The index of the texure a material uses is given
        // by 'material.texDiffuseIdx'. However, the texture index is optional, as some materials do not use a
        // diffuse texture at all. You can use 'optional.has_value()' to check if a material index is defined.
        // There are multiple ways to handle this. You would likely want different pipelines with other shaders
        // for different materials in an actual renderer. Here, you can simply bind the first textures as a
        // fallback if there is no texture defined.

        // TODO: Assignment 2.12
        // Write the uniform buffer in addition to the combined image sampler to the descriptor sets.
    }

    //--------------------------------------------------------------------------------------------------

    void recordCommandBuffer(VkCommandBuffer commandBuffer) {
        const uint32_t currentFrame = m_app.getSwapchain().getCurrentFrameIdx();
        const VkExtent2D extent = m_app.getRenderTargetExtent();

        // TODO: Assignment 2.1
        // First, begin the render pass. Remember to set the render area and the clear value for the color
        // attachment. The subpass contents can be set to 'VK_SUBPASS_CONTENTS_INLINE'. Next, dynamically write
        // the viewport and scissor using the extent from above. Then, bind the graphics pipeline. For now,
        // draw three vertices using 'vkCmdDraw(commandBuffer, 3, 1, 0, 0)'. Their positions are currently
        // hard-coded into the vertex shader. Finally, don't forget to end the render pass.
        //
        // [ New functions: vkCmdBeginRenderPass, vkCmdSetViewport, vkCmdSetScissor, vkCmdBindPipeline,
        //                  vkCmdDraw, vkCmdEndRenderPass ]

        // TODO: Assignment 2.2
        // Now, bind the vertex buffer before making the draw call.
        // Then, update the draw call to render six vertices instead of three.
        //
        // [ New functions: vkCmdBindVertexBuffers ]

        // TODO: Assignment 2.3
        // After binding the vertex buffer, also bind the index buffer.
        // Then update the draw call to use the index  buffer.
        //
        // [ New functions: vkCmdBindIndexBuffer, vkCmdDrawIndexed ]

        // TODO: Assignment 2.4
        // Before the draw call, push the push constants for the vertex buffer.
        // You can calculate the MVP matrix by 'm_app.getProjMat() * m_app.getViewMat()'.
        //
        // [ New functions: vkCmdPushConstants ]

        // TODO: Assignment 2.6
        // Update the number of indices to match the mesh and apply the node's model matrix to the MVP matrix.

        // TODO: Assignment 2.7
        // Add a second clear value for the depth image and set it to one for the initial depth.

        // TODO: Assignment 2.8
        // Bind the descriptor set before the draw call.

        // TODO: Assignment 2.9
        // Submit a draw call for each node (not mesh!) of the scene. For each node, create push constants using
        // the transform of the node, and bind the vertex and index buffers for the mesh referenced by the node.
        // Remember that the node and mesh indices are not the same, as multiple nodes can reference the same mesh.
        // Then, submit a draw call for each node, using the correct number of indices.

        // TODO: Assignment 2.10
        // Bind the descriptor set corresponding to the material of the mesh of the current node before submitting
        // the draw calls.

        // TODO: Assignment 2.11
        // When making the draw calls, first draw all the opaque meshes, then all teh transparent meshes. A
        // material stores whether it is transparent in the 'material.alphaMode' (Scene::Material::AlphaMode::Opaque)
        // field. You might want to extract everything corresponding to a draw call into a seperate function.
    }


    //--------------------------------------------------------------------------------------------------

    void buildUi() {
        if (m_scene.nodes.size() > 0) {
            ImGui::Text("Scene '%s'", m_scene.name.c_str());
            ImGui::Separator();
            ImGui::Text("Nodes     : %d", static_cast<uint32_t>(m_scene.nodes.size()));
            ImGui::Text("Meshes    : %d", static_cast<uint32_t>(m_scene.meshes.size()));
            ImGui::Text("Materials : %d", static_cast<uint32_t>(m_scene.materials.size()));
            ImGui::Text("Textures  : %d", static_cast<uint32_t>(m_scene.textures.size()));
        }
    }

    //--------------------------------------------------------------------------------------------------

    Buffer uploadBufferWithStaging(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
        const VmaAllocator allocator = m_app.getAllocator();

        Buffer stagingBuffer{};
        VK_CHECK(stagingBuffer.init(
            allocator,
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));

        void* mapped;
        VK_CHECK(vmaMapMemory(allocator, stagingBuffer.allocation, &mapped));
        std::memcpy(mapped, data, size);
        vmaUnmapMemory(allocator, stagingBuffer.allocation);

        Buffer resultBuffer{};
        VK_CHECK(resultBuffer.init(
            allocator,
            size,
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // required for copy
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        ));

        VK_CHECK(m_app.executeSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            const VkBufferCopy copyRegion{.size = size};
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, resultBuffer.buffer, 1, &copyRegion);
        }));

        stagingBuffer.deinit(allocator);

        return resultBuffer;
    }

    Image uploadImageWithStaging(
        const void* data,
        VkDeviceSize size,
        VkImageType imageType,
        VkFormat format,
        VkExtent3D extent,
        VkImageUsageFlags usage,
        VkImageLayout finalLayout
    ) {
        const VmaAllocator allocator = m_app.getAllocator();

        Buffer stagingBuffer{};
        stagingBuffer.init(
            allocator,
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        void* mapped;
        VK_CHECK(vmaMapMemory(allocator, stagingBuffer.allocation, &mapped));
        std::memcpy(mapped, data, size);
        vmaUnmapMemory(allocator, stagingBuffer.allocation);

        Image resultImage{};
        resultImage.init(
            allocator,
            imageType,
            format,
            extent,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_TILING_OPTIMAL,
            usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        VK_CHECK(m_app.executeSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            const auto transferLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            resultImage.transitionLayout(commandBuffer, format, VK_IMAGE_LAYOUT_UNDEFINED, transferLayout);

            const VkBufferImageCopy region{
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageOffset = {0, 0, 0},
                .imageExtent = extent,
            };
            vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, resultImage.image, transferLayout, 1, &region);

            resultImage.transitionLayout(commandBuffer, format, transferLayout, finalLayout);
        }));

        stagingBuffer.deinit(allocator);

        return resultImage;
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
