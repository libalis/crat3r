#include <volk.h>
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
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

const std::string SCENE = "assets/tokyo.glb";

//--------------------------------------------------------------------------------------------------

struct alignas(16) PointLight {
    glm::vec3 pos;
    float radius;
    glm::vec3 col;
    float intensity;
};

constexpr glm::vec3 hexColor(uint32_t h) {
    return glm::vec3((h >> 16) & 0xFF, (h >> 8) & 0xFF, h & 0xF) / 255.f;
}

constexpr uint32_t MAX_LIGHTS = 8;

constexpr PointLight POINT_LIGHTS[MAX_LIGHTS] = {
    PointLight{glm::vec3(0.204881f, 0.109195f, -0.11035f), 0.16f, hexColor(0xFF6AAF), 18.f},
    PointLight{glm::vec3(0.204881f, 0.109195f, -0.073441f), 0.16f, hexColor(0x6573FF), 18.f},
    PointLight{glm::vec3(0.204881f, 0.109195f, -0.036167f), 0.16f, hexColor(0x51A1FF), 18.f},
    PointLight{glm::vec3(-0.108069f, 0.023262f, -0.239644f), 0.2f, hexColor(0xFFA740), 18.f},
    PointLight{glm::vec3(0.210774f, 0.021687f, 0.264325f), 0.2f, hexColor(0xFFA740), 12.f},
    PointLight{glm::vec3(0.028178f, -0.031176f, 0.199417f), 0.1f, hexColor(0x58FF4D), 8.f},
    PointLight{glm::vec3(0.042368f, -0.031176f, 0.214418f), 0.1f, hexColor(0x58FF4D), 8.f},
    PointLight{glm::vec3(-0.144686f, -0.069874f, 0.090418f), 0.2f, hexColor(0xFF731D), 5.f},
};

//--------------------------------------------------------------------------------------------------

constexpr VkFormat SHADOW_MAP_FORMAT = VK_FORMAT_D32_SFLOAT;
constexpr VkExtent2D SHADOW_MAP_EXTENT = {2048, 2048};

constexpr VkSampleCountFlagBits MSAA_SAMPLES = VK_SAMPLE_COUNT_8_BIT;

//--------------------------------------------------------------------------------------------------

// TODO: Assignment 3.x
// Modify these structs and add new ones as necessary.

struct VertexPushConstants {
    glm::mat4 model;
    glm::mat4 normal;
};

struct MaterialUniformBuffer {
    glm::vec4 baseColor;
    int32_t hasTexture;
};

// 3.1
struct FrameUniformBuffer {
    glm::mat4 VP;
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
        createMaterialDescriptorSetLayout();
        createFrameDescriptorSetLayout();
        createPipelineLayout();
        createPipeline();

        createColorResources();
        createDepthResources();
        createFramebuffer();

        createShadowRenderPass();
        createShadowPipelineLayout();
        createShadowPipeline();
        createShadowImage();
        createShadowSampler();
        createShadowFramebuffer();

        createVertexBuffers();
        createIndexBuffers();

        createTextures();
        createTextureSampler();

        createMaterialUniformBuffers();
        createFrameUniformBuffers();

        createDescriptorPool();
        createMaterialDescriptorSets();
        createFrameDescriptorSets();
    }

    void run() {
        m_app.run(
            [&]() { // onRenderUi
                buildUi();
            },
            [&](VkCommandBuffer commandBuffer) { // onRender
                m_scene.updateAnimationState(m_app.getDeltaTime());
                recordCommandBufferShadow(commandBuffer);
                recordCommandBuffer(commandBuffer);
            },
            [&]() { // onResize
                freeColorResources();
                freeDepthResources();
                freeFramebuffer();

                createColorResources();
                createDepthResources();
                createFramebuffer();
            }
        );
    }

    void deinit() {
        const VkDevice device = m_app.getDevice();
        const VmaAllocator allocator = m_app.getAllocator();

        vkDestroyDescriptorSetLayout(device, m_materialDescriptorSetLayout, nullptr);
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);

        vkDestroySampler(device, m_textureSampler, nullptr);
        for (const auto& imageView : m_textureImageViews)
            vkDestroyImageView(device, imageView, nullptr);
        for (auto& image : m_textureImages)
            image.deinit(allocator);

        for (auto& buf : m_materialUniformBuffers)
            buf.deinit(allocator);

        for (auto& buf : m_vertexBuffers)
            buf.deinit(allocator);
        for (auto& buf : m_indexBuffers)
            buf.deinit(allocator);

        freeFramebuffer();
        freeDepthResources();
        freeColorResources();

        vkDestroyPipeline(device, m_pipeline, nullptr);
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        vkDestroyRenderPass(device, m_renderPass, nullptr);

        // TODO: Assignment 3.x
        // Destroy and free any ressources you have created.

        // 3.1
        vkDestroyDescriptorSetLayout(device, m_frameDescriptorSetLayout, nullptr);
        for(size_t i = 0; i < m_frameUniformBuffers.size(); i++) {
            vmaUnmapMemory(allocator, m_frameUniformBuffers[i].allocation);
            m_frameUniformBuffers[i].deinit(allocator);
        }

        m_app.deinit();
    }

  private:
    VulkanApp m_app;
    Scene m_scene;

    VkRenderPass m_renderPass;
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_pipeline;

    Image m_depthImage;
    VkImageView m_depthImageView;
    VkFramebuffer m_framebuffer;

    VkDescriptorPool m_descriptorPool;
    VkDescriptorSetLayout m_materialDescriptorSetLayout;
    std::vector<VkDescriptorSet> m_materialDescriptorSets;

    std::vector<Buffer> m_vertexBuffers;
    std::vector<Buffer> m_indexBuffers;

    std::vector<Buffer> m_materialUniformBuffers;

    std::vector<Image> m_textureImages;
    std::vector<VkImageView> m_textureImageViews;
    VkSampler m_textureSampler;

    // TODO: Assignment 3.x
    // Add any other necessary class members here.

    // 3.1
    VkDescriptorSetLayout m_frameDescriptorSetLayout;
    std::vector<Buffer> m_frameUniformBuffers;
    std::vector<void*> m_frameUniformBuffersData;
    std::vector<VkDescriptorSet> m_frameDescriptorSets;

    struct TransparentNode {
        glm::mat4 transform;
        uint32_t meshIdx;
        float dist2;
    };
    std::vector<TransparentNode> m_transparentNodes;

    bool m_sunRotate = true;
    float m_sunTime = 0.f;
    int m_selectedLighting = 0;

    glm::vec3 m_sunPos{};
    glm::vec3 m_sunDir{};
    glm::vec3 m_sunCol{};
    glm::mat4 m_sunViewProj{};
    float m_sunIntensity = 0.f;
    float m_ambientIntensity = 0.f;
    uint32_t m_numLights = 0;

  private:
    //--------------------------------------------------------------------------------------------------

    void createRenderPass() {
        // TODO: Assignment 3.7
        // Set the sample count of the current color and depth attachments to 'MSAA_SAMPLES'. Then, add
        // another color attachment with one sample and add it as a resolve attachment to the subpass.

        const VkFormat targetFormat = m_app.getRenderTargetFormat();

        const VkAttachmentDescription colorAttachment{
            .format = targetFormat,
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

        const VkAttachmentDescription depthAttachment{
            .format = VK_FORMAT_D32_SFLOAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

        const VkAttachmentReference depthAttachmentRef{
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

        const VkSubpassDescription subpass{
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentRef,
            .pDepthStencilAttachment = &depthAttachmentRef,
        };

        const VkSubpassDependency colorDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        };

        const VkSubpassDependency depthDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        };

        const std::array attachments = {colorAttachment, depthAttachment};
        const std::array dependencies = {colorDependency, depthDependency};

        const VkRenderPassCreateInfo renderPassInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = attachments.size(),
            .pAttachments = attachments.data(),
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = dependencies.size(),
            .pDependencies = dependencies.data(),
        };

        VK_CHECK(vkCreateRenderPass(m_app.getDevice(), &renderPassInfo, nullptr, &m_renderPass));
    }

    void createMaterialDescriptorSetLayout() {
        const std::array layoutBindings = {
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };

        const VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = layoutBindings.size(),
            .pBindings = layoutBindings.data(),
        };

        VK_CHECK(vkCreateDescriptorSetLayout(m_app.getDevice(), &layoutInfo, nullptr, &m_materialDescriptorSetLayout));
    }

    void createFrameDescriptorSetLayout() {
        // TODO: Assignment 3.1
        // Create a descriptor set layout for one uniform buffer at binding zero in the vertex shader.

        const std::array layoutBindings = {
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            }
        };
        const VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = layoutBindings.size(),
            .pBindings = layoutBindings.data(),
        };
        VK_CHECK(vkCreateDescriptorSetLayout(m_app.getDevice(), &layoutInfo, nullptr, &m_frameDescriptorSetLayout));

        // TODO: Assignment 3.2
        // Add a second binding for a uniform buffer for the fragment stage.

        // TODO: Assignment 3.4
        // Add a third binding for the shadow map in the fragment stage.
    }

    void createPipelineLayout() {
        // TODO: Assignment 3.1
        // Add the second descriptor set layout to the pipeline layout.

        const VkPushConstantRange pushConstant{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(VertexPushConstants),
        };

        const std::array setLayouts{
            m_materialDescriptorSetLayout,
            m_frameDescriptorSetLayout
        };

        const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = setLayouts.size(),
            .pSetLayouts = setLayouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstant,
        };

        VK_CHECK(vkCreatePipelineLayout(m_app.getDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout));
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
        // TODO: Assignment 3.1
        // Add another vertex input attribute description for the vertex normals.

        // TODO: Assignment 3.7
        // Update the sample count in the multisampling state info.

        const auto vertShaderCode = Fs::readFile("shaders/a03/forward.vert.spv");
        const auto fragShaderCode = Fs::readFile("shaders/a03/forward.frag.spv");

        const VkShaderModule vertShaderModule = createShaderModule(m_app.getDevice(), vertShaderCode);
        const VkShaderModule fragShaderModule = createShaderModule(m_app.getDevice(), fragShaderCode);

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
                .stride = sizeof(Scene::Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            },
        };

        const std::array attributeDescriptions{
            VkVertexInputAttributeDescription{
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Scene::Vertex, pos),
            },
            VkVertexInputAttributeDescription{
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(Scene::Vertex, uv),
            },
            VkVertexInputAttributeDescription{
                .location = 2,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Scene::Vertex, normal)
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
            .cullMode = VK_CULL_MODE_BACK_BIT,
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
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT,
        };

        const VkPipelineDepthStencilStateCreateInfo depthStencilState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
        };

        const VkPipelineColorBlendStateCreateInfo colorBlending{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment,
        };

        const std::array dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE
        };

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

        VK_CHECK(vkCreateGraphicsPipelines(m_app.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));

        vkDestroyShaderModule(m_app.getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(m_app.getDevice(), vertShaderModule, nullptr);
    }

    void createColorResources() {
        const VkFormat format = m_app.getRenderTargetFormat();
        const VkExtent2D extent = m_app.getRenderTargetExtent();

        // TODO: Assignment 3.7
        // Create an image where the multisampling color samples are stored before being resovlved by the
        // render pipeline. Then, create an image view for this image.
    }

    void freeColorResources() {
        // TODO: Assignment 3.7
        // Destroy the multisampling color image and view.
    }

    void createDepthResources() {
        // TODO: Assignment 3.7
        // Update the sample count of the depth image.

        const VkFormat format = VK_FORMAT_D32_SFLOAT;
        const VkExtent2D extent = m_app.getRenderTargetExtent();

        m_depthImage.init(
            m_app.getAllocator(),
            VK_IMAGE_TYPE_2D,
            format,
            {extent.width, extent.height, 1},
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        const VkImageViewCreateInfo imageViewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_depthImage.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
        };
        VK_CHECK(vkCreateImageView(m_app.getDevice(), &imageViewInfo, nullptr, &m_depthImageView));
    }

    void freeDepthResources() {
        vkDestroyImageView(m_app.getDevice(), m_depthImageView, nullptr);
        m_depthImage.deinit(m_app.getAllocator());
    }

    void createFramebuffer() {
        // TODO: Assignment 3.7
        // Update the framebuffer attachment so that the samples are first rendered to the multisampling
        // color image and finally resolved to the render target image.

        const std::array attachments = {m_app.getRenderTargetImageView(), m_depthImageView};
        const VkExtent2D imageExtent = m_app.getRenderTargetExtent();

        const VkFramebufferCreateInfo framebufferInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = m_renderPass,
            .attachmentCount = attachments.size(),
            .pAttachments = attachments.data(),
            .width = imageExtent.width,
            .height = imageExtent.height,
            .layers = 1,
        };

        VK_CHECK(vkCreateFramebuffer(m_app.getDevice(), &framebufferInfo, nullptr, &m_framebuffer));
    }

    void freeFramebuffer() { //
        vkDestroyFramebuffer(m_app.getDevice(), m_framebuffer, nullptr);
    }

    void createShadowRenderPass() {
        // TODO: Assignment 3.4
        // Create a render pass for the shadow pass. Add only a depth attachment of format 'SHADOW_MAP_FORMAT'
        // and no color attachments. To read the shadow map later on, the depth attachment has to be in the layout
        // 'VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL' after this render pass. Additionaly, add two
        // dependencies. The first from the fragment shader of an external subpass to the early fragment tests
        // of this subpass. The second from the late fragment tests of this subpass to the fragment shader
        // of the external subpass. These dependencies ensure that the shadow render pass does not 'overtake'
        // the color render pass during rendering.
    }

    void createShadowPipelineLayout() {
        // TODO: Assignment 3.4
        // Create a pipeline layout for the shadow pass without any descriptor sets and a single push constant
        // range. Add a struct for the shadow push constants to this file, which contains a model-view-projection
        // matrix.
    }

    void createShadowPipeline() {
        // TODO: Assignment 3.4
        // Create a pipeline for the shadow pass. Add 'shadow.vert' as a vertex shader and no fragment shader.
        // As vertex input attributes bind only the vertex position. Color blending can be completely disabled
        // for tis pipeline.
    }

    void createShadowImage() {
        // TODO: Assignment 3.4
        // Create a 2D depth image of format 'SHADOW_MAP_FORMAT' and extent 'SHADOW_MAP_EXTENT' to render the
        // shadow map to. The image usage flags have to include 'VK_IMAGE_USAGE_SAMPLED_BIT' to be able to
        // read it in the fragment shader. Additionaly, create an image view for the depth image.
    }

    void createShadowSampler() {
        // TODO: Assignment 3.4
        // Create a sampler for the shadow map. This sampler should use nearest neighbor filtering
        // instead of bilinear filtering.
    }

    void createShadowFramebuffer() {
        // TODO: Assignment 3.4
        // Create a framebuffer that contains the shadow depth image.
    }

    void createVertexBuffers() {
        m_vertexBuffers.reserve(m_scene.meshes.size());

        for (const auto& mesh : m_scene.meshes) {
            const VkDeviceSize size = sizeof(mesh.vertices[0]) * mesh.vertices.size();

            m_vertexBuffers.push_back(
                uploadBufferWithStaging(mesh.vertices.data(), size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            );
        }
    }

    void createIndexBuffers() {
        m_indexBuffers.reserve(m_scene.meshes.size());

        for (const auto& mesh : m_scene.meshes) {
            const VkDeviceSize size = sizeof(mesh.indices[0]) * mesh.indices.size();

            m_indexBuffers.push_back(
                uploadBufferWithStaging(mesh.indices.data(), size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
            );
        }
    }

    void createTextures() {
        const VmaAllocator allocator = m_app.getAllocator();

        m_textureImages.reserve(m_scene.textures.size());
        m_textureImageViews.reserve(m_scene.textures.size());

        for (const auto& texture : m_scene.textures) {
            const uint8_t* imageData = texture.data.data();
            const VkDeviceSize imageSize = texture.data.size();
            const VkExtent3D extent = {texture.width, texture.height, 1};

            Image image = uploadImageWithStaging(
                imageData,
                imageSize,
                VK_IMAGE_TYPE_2D,
                texture.format,
                extent,
                VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );

            // image view
            const VkImageViewCreateInfo viewInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = texture.format,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            };

            VkImageView imageView;
            VK_CHECK(vkCreateImageView(m_app.getDevice(), &viewInfo, nullptr, &imageView));

            m_textureImages.push_back(image);
            m_textureImageViews.push_back(imageView);
        }
    }

    void createTextureSampler() {
        const VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .mipLodBias = 0.f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.f,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.f,
            .maxLod = 0.f,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };

        VK_CHECK(vkCreateSampler(m_app.getDevice(), &samplerInfo, nullptr, &m_textureSampler))
    }

    void createMaterialUniformBuffers() {
        // TODO: Assignment 3.3
        // Write the 'metallic' and 'roughness' parameters from the materials to the uniform buffers.

        m_materialUniformBuffers.reserve(m_scene.materials.size());

        for (const auto& material : m_scene.materials) {
            const MaterialUniformBuffer uniformBuffer{
                .baseColor = glm::vec4(material.colDiffuse, material.opacity),
                .hasTexture = material.texDiffuseIdx.has_value(),
            };

            m_materialUniformBuffers.push_back(uploadBufferWithStaging(
                &uniformBuffer, sizeof(MaterialUniformBuffer), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
            ));
        }
    }

    void createFrameUniformBuffers() {
        // TODO: Assignment 3.1
        // Create uniform buffers for the per-frame data in host-visible and host-coherent memory,
        // and map them to CPU memory.

        const uint32_t framesInFlight = m_app.getSwapchain().getMaxFramesInFlight();
        const VmaAllocator allocator = m_app.getAllocator();

        m_frameUniformBuffers.resize(framesInFlight);
        m_frameUniformBuffersData.resize(framesInFlight);
        for(size_t i = 0; i < framesInFlight; i++) {
            VK_CHECK(m_frameUniformBuffers[i].init(
                allocator,
                sizeof(FrameUniformBuffer),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            ));
            VK_CHECK(vmaMapMemory(allocator, m_frameUniformBuffers[i].allocation, &m_frameUniformBuffersData[i]));
        }
    }

    void createDescriptorPool() {
        const uint32_t numMaterials = m_scene.materials.size();
        const uint32_t framesInFlight = m_app.getSwapchain().getMaxFramesInFlight();

        const std::array poolSizes = {
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = numMaterials + framesInFlight,
            },
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = numMaterials,
            },
        };

        const VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = numMaterials + framesInFlight,
            .poolSizeCount = poolSizes.size(),
            .pPoolSizes = poolSizes.data(),
        };

        VK_CHECK(vkCreateDescriptorPool(m_app.getDevice(), &poolInfo, nullptr, &m_descriptorPool));
    }

    void createMaterialDescriptorSets() {
        const uint32_t numMaterials = m_scene.materials.size();

        const std::vector<VkDescriptorSetLayout> layouts(numMaterials, m_materialDescriptorSetLayout);

        const VkDescriptorSetAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_descriptorPool,
            .descriptorSetCount = numMaterials,
            .pSetLayouts = layouts.data(),
        };

        m_materialDescriptorSets.resize(numMaterials);
        VK_CHECK(vkAllocateDescriptorSets(m_app.getDevice(), &allocInfo, m_materialDescriptorSets.data()));

        for (uint32_t i = 0; i < numMaterials; i++) {
            const auto& material = m_scene.materials[i];

            const VkDescriptorImageInfo imageInfo{
                .sampler = m_textureSampler,
                .imageView = m_textureImageViews[material.texDiffuseIdx.value_or(0)],
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            const VkDescriptorBufferInfo bufferInfo{
                .buffer = m_materialUniformBuffers[i].buffer,
                .offset = 0,
                .range = VK_WHOLE_SIZE,
            };

            const std::array descriptorWrites = {
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_materialDescriptorSets[i],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &imageInfo,
                },
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_materialDescriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &bufferInfo,
                },
            };

            vkUpdateDescriptorSets(m_app.getDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
    }

    void createFrameDescriptorSets() {
        const uint32_t framesInFlight = m_app.getSwapchain().getMaxFramesInFlight();

        // TODO: Assignment 3.1
        // Allocate the per-frame descriptor sets and write the uniform buffers to them.

        const std::vector<VkDescriptorSetLayout> layouts(framesInFlight, m_frameDescriptorSetLayout);
        const VkDescriptorSetAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_descriptorPool,
            .descriptorSetCount = framesInFlight,
            .pSetLayouts = layouts.data()
        };
        m_frameDescriptorSets.resize(framesInFlight);
        VK_CHECK(vkAllocateDescriptorSets(m_app.getDevice(), &allocInfo, m_frameDescriptorSets.data()));
        for(uint32_t i = 0; i < framesInFlight; i++) {
            const VkDescriptorBufferInfo bufferInfo{
                .buffer = m_frameUniformBuffers[i].buffer,
                .offset = 0,
                .range = VK_WHOLE_SIZE
            };
            const std::array descriptorWrites = {
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_frameDescriptorSets[i],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &bufferInfo
                }
            };
            vkUpdateDescriptorSets(m_app.getDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }

        // TODO: Assignment 3.2
        // Update the descriptor writes so that the vertex descriptor only covers the view-projection matrix
        // and the fragment descriptor only covers the remaining buffer. Use 'sizeof' and 'offsetof' to find
        // the appropriate  values for the 'offset' and 'range' parameters.

        // TODO: Assignment 3.4
        // Write the shadow map to the descriptor sets.
    }

    //--------------------------------------------------------------------------------------------------

    void recordCommandBufferShadow(VkCommandBuffer commandBuffer) {
        // TODO: Assignment 3.4
        // Record the shadow render pass to the command buffer, similary to 'recordCommandBuffer'.
        // Here are a few things to keep in mind:
        // - There is only a depth image to clear.
        // - The extent of the area to render to is not the same as the screen.
        // - The shadow pipeline uses no descriptor sets.
        // - Only opaque objects should cast shadows.
        // - The shadow 'camera' uses the view-projection matrix 'm_sunViewProj'.

        const uint32_t currentFrame = m_app.getSwapchain().getCurrentFrameIdx();
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer) {
        // TODO: Assignment 3.1
        // Bind the per-frame descriptor set once before submitting the first draw call.

        const uint32_t currentFrame = m_app.getSwapchain().getCurrentFrameIdx();
        const VkExtent2D extent = m_app.getRenderTargetExtent();

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipelineLayout,
            1,
            1,
            &m_frameDescriptorSets[currentFrame],
            0,
            nullptr
        );

        updateFrameUniformBuffer(currentFrame);

        const VkClearColorValue clearDay{0.55f, 0.75f, 0.9f, 1.0f};
        const VkClearColorValue clearNight{0.003f, 0.002f, 0.002f, 1.f};
        const std::array clearValues = {
            VkClearValue{.color = (m_selectedLighting == 0) ? clearDay : clearNight},
            VkClearValue{.depthStencil = {1.f, 0}},
        };

        const VkRenderPassBeginInfo renderPassInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = m_renderPass,
            .framebuffer = m_framebuffer,
            .renderArea{.offset = {0, 0}, .extent = extent},
            .clearValueCount = clearValues.size(),
            .pClearValues = clearValues.data(),
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

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        const glm::mat4 viewProj = m_app.getProjMat() * m_app.getViewMat();

        vkCmdSetDepthWriteEnable(commandBuffer, VK_TRUE);

        m_transparentNodes.clear();

        m_scene.traverseNodes([&](const glm::mat4& transform, uint32_t meshIdx) {
            if (m_scene.materials[m_scene.meshes[meshIdx].materialIdx].alphaMode ==
                Scene::Material::AlphaMode::Opaque) {
                makeDrawCall(commandBuffer, viewProj, transform, meshIdx);
            } else {
                const glm::vec3 d = glm::vec3(transform[3]) - m_app.getCameraPos();
                m_transparentNodes.push_back({transform, meshIdx, glm::dot(d, d)});
            }
        });

        vkCmdSetDepthWriteEnable(commandBuffer, VK_FALSE);

        std::sort(m_transparentNodes.begin(), m_transparentNodes.end(), [&](const auto& a, const auto& b) {
            return a.dist2 > b.dist2;
        });

        for (const auto& node : m_transparentNodes) {
            makeDrawCall(commandBuffer, viewProj, node.transform, node.meshIdx);
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    void makeDrawCall(
        VkCommandBuffer commandBuffer, const glm::mat4& viewProj, const glm::mat4& model, uint32_t meshIdx
    ) {
        // TODO: Assignment 3.1
        // Now, write the model and normal matrix to the push constants. The normal matrix can be obtained
        // by using 'glm::inverseTranspose(glm::mat3(model))'.

        const auto& mesh = m_scene.meshes[meshIdx];
        const auto& vertexBuffer = m_vertexBuffers[meshIdx];
        const auto& indexBuffer = m_indexBuffers[meshIdx];

        const size_t numIndices = mesh.indices.size();
        const uint32_t materialIdx = mesh.materialIdx;

        const VertexPushConstants pc{
            .model = model,
            .normal = glm::mat4(glm::inverseTranspose(glm::mat3(model)))
        };
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipelineLayout,
            0,
            1,
            &m_materialDescriptorSets[materialIdx],
            0,
            nullptr
        );

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.buffer, &offset);

        vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
    }

    void updateFrameUniformBuffer(uint32_t currentFrame) {
        // TODO: Assignment 3.1
        // Write the view-projection matrix to the uniform buffer of the current frame.

        FrameUniformBuffer* frameUniformBuffer = static_cast<FrameUniformBuffer*>(m_frameUniformBuffersData[currentFrame]);
        frameUniformBuffer->VP = m_app.getProjMat() * m_app.getViewMat();

        // TODO: Assignment 3.2
        // Write the camera position, the sun's direction, color, and intensity, as well as the ambient
        // intensity, to the uniform buffer. You can obtain them from 'm_app.getCameraPos()', 'm_sunDir',
        // 'm_sunCol', 'm_sunIntensity', and 'm_ambientIntensity'.

        // TODO: Assignment 3.4
        // Add the sun's view-projection matrix ('m_sunViewProj') to the uniform buffer.

        // TODO: Assignment 3.6
        // Add the array of point lights (POINT_LIGHTS) and the number of point lights (m_numLights) to
        // the per-frame uniform buffer. Set the size of the array in the uniform buffer to MAX_LIGHTS.
    }

    //--------------------------------------------------------------------------------------------------

    void buildUi() {
        ImGui::Text("Lighting");
        ImGui::Separator();

        static const char* items[]{"Daytime", "Nighttime"};
        ImGui::Combo("##Lighting", &m_selectedLighting, items, IM_ARRAYSIZE(items));
        if (m_selectedLighting == 0) {
            m_sunIntensity = 5.f;
            m_ambientIntensity = 0.1f;
            m_numLights = 0;
        } else {
            m_sunIntensity = 0.f;
            m_ambientIntensity = 0.01f;
            m_numLights = MAX_LIGHTS;
        }

        ImGui::Checkbox("Rotate Sun", &m_sunRotate);
        if (m_sunRotate) {
            m_sunTime += m_app.getDeltaTime() * 0.2f;
        }

        m_sunPos = {glm::cos(m_sunTime), 1.f, glm::sin(m_sunTime)};
        m_sunDir = glm::normalize(-m_sunPos);
        m_sunCol = glm::vec3(1.f, 0.92f, 0.82f);

        glm::mat4 view = glm::lookAt(m_sunPos, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
        glm::mat4 proj = glm::ortho(-0.5f, 0.5f, -0.5f, 0.5f, 1.f, 2.f);
        proj[1][1] *= -1.f;
        m_sunViewProj = proj * view;

        ImGui::NewLine();

        if (!m_scene.nodes.empty()) {
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

        Image resultImage{};
        VK_CHECK(resultImage.init(
            allocator,
            imageType,
            format,
            extent,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_TILING_OPTIMAL,
            usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        ));

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
