#include "vulkan_app.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_null.h>
#include <imgui_impl_vulkan.h>

#include "check_error.hpp"
#include "context.hpp"
#include "core/filesystem.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <thread>

//--------------------------------------------------------------------------------------------------

constexpr uint32_t API_VERSION = VK_API_VERSION_1_3;

//--------------------------------------------------------------------------------------------------

static void glfwErrorCallback(const int error_code, const char* description) {
    fprintf(stderr, "[ERROR] glfw: %s (code: %d)\n", description, error_code);
}

//--------------------------------------------------------------------------------------------------

VkResult VulkanApp::init(const char* title, uint32_t windowW, uint32_t windowH, bool vsync) {
    glfwSetErrorCallback(glfwErrorCallback);

    if (glfwInit() != GLFW_TRUE) {
        fprintf(stderr, "glfwInit failed! Running in headless mode...\n");
        m_headless = true;
    }

    m_windowTitle = title;
    m_windowSize.width = windowW;
    m_windowSize.height = windowH;
    m_vsync = vsync;

    VK_CHECK_RETURN(createContext());
    VK_CHECK_RETURN(createWindow());
    VK_CHECK_RETURN(createSwapchain());
    VK_CHECK_RETURN(createCommandPools());
    VK_CHECK_RETURN(createCommandBuffers());
    VK_CHECK_RETURN(createAllocator());

    VK_CHECK_RETURN(setupImgui());
    VK_CHECK_RETURN(setupImguiStyle());
    VK_CHECK_RETURN(createFrameResources());
    setupAudio();

    return VK_SUCCESS;
}

VkResult VulkanApp::createContext() {
    std::vector<const char*> instanceExtensions{};
    if (!m_headless) {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        instanceExtensions.insert(instanceExtensions.end(), glfwExtensions, glfwExtensions + glfwExtensionCount);
    }

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .accelerationStructure = VK_TRUE,
    };
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .rayQuery = VK_TRUE,
    };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .rayTracingPipeline = VK_TRUE,
    };
    VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR rtPositionFetchFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR,
        .rayTracingPositionFetch = VK_TRUE,
    };

    std::vector<ExtensionInfo> deviceExtensions{};
    if (!m_headless) {
        deviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    if (m_enableRTX) {
        deviceExtensions.emplace_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        deviceExtensions.emplace_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &accelFeatures);
        deviceExtensions.emplace_back(VK_KHR_RAY_QUERY_EXTENSION_NAME, &rayQueryFeatures);
        deviceExtensions.emplace_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &rtPipelineFeatures);
        deviceExtensions.emplace_back(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME, &rtPositionFetchFeatures);
    }

    const ContextInitInfo ctxInfo{
        .applicationName = "CRAT3R",
        .apiVersion = API_VERSION,
        .instanceExtensions = std::move(instanceExtensions),
        .deviceExtensions = std::move(deviceExtensions),
        .queues = {VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT},
    };

    VK_CHECK_RETURN(m_context.init(ctxInfo));

    m_queueGCT = m_context.getQueueInfo(0);

    return VK_SUCCESS;
}

void VulkanApp::enableRTX() {
    if (m_window != nullptr) {
        fprintf(stderr, "enableRTX() must be called before init()!\n");
        return;
    }

    m_enableRTX = true;
}

VkResult VulkanApp::createWindow() {
    if (m_headless) {
        return VK_SUCCESS;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE); // Aware of DPI scaling

    m_window = glfwCreateWindow(m_windowSize.width, m_windowSize.height, m_windowTitle, nullptr, nullptr);
    if (!m_window) {
        fprintf(stderr, "glfwCreateWindow failed!\n");
        return VK_ERROR_UNKNOWN;
    }

    glfwSetWindowSize(m_window, m_windowSize.width, m_windowSize.height);

    VK_CHECK_RETURN(glfwCreateWindowSurface(getInstance(), m_window, nullptr, &m_surface));

    return VK_SUCCESS;
}

VkResult VulkanApp::createSwapchain() {
    const SwapchainInitInfo swpInfo{
        .device = getDevice(),
        .physcialDevice = getPhysicalDevice(),
        .queue = m_queueGCT.queue,
        .queueFamilyIndex = m_queueGCT.familyIndex,
        .surface = m_surface,
        .headless = m_headless,
    };

    VK_CHECK_RETURN(m_swapchain.init(swpInfo));
    VK_CHECK_RETURN(m_swapchain.update(m_windowSize, m_vsync));

    return VK_SUCCESS;
}

VkResult VulkanApp::createCommandPools() {
    {
        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = m_queueGCT.familyIndex,
        };

        VK_CHECK_RETURN(vkCreateCommandPool(getDevice(), &poolInfo, nullptr, &m_commandPool));
    }

    {
        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = m_queueGCT.familyIndex,
        };

        VK_CHECK_RETURN(vkCreateCommandPool(getDevice(), &poolInfo, nullptr, &m_transientCommandPool));
    }

    return VK_SUCCESS;
}

VkResult VulkanApp::createCommandBuffers() {
    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = m_swapchain.getMaxFramesInFlight(),
    };

    m_commandBuffers.resize(m_swapchain.getMaxFramesInFlight());
    VK_CHECK_RETURN(vkAllocateCommandBuffers(getDevice(), &allocInfo, m_commandBuffers.data()));

    return VK_SUCCESS;
}

VkResult VulkanApp::createAllocator() {
    VmaVulkanFunctions vulkanFunctions = {};

    const VmaAllocatorCreateInfo allocatorInfo{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = getPhysicalDevice(),
        .device = getDevice(),
        .pVulkanFunctions = &vulkanFunctions,
        .instance = getInstance(),
        .vulkanApiVersion = API_VERSION,
    };

    VK_CHECK_RETURN(vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions));

    VK_CHECK_RETURN(vmaCreateAllocator(&allocatorInfo, &m_allocator));

    return VK_SUCCESS;
}

VkResult VulkanApp::setupImgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    if (m_headless) {
        ImGui_ImplNull_Init();
        return VK_SUCCESS;
    }

    ImGui_ImplGlfw_InitForVulkan(m_window, true);

    const VkFormat swapChainFormat = m_swapchain.getFormat();

    const VkPipelineRenderingCreateInfo pipelineRenderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapChainFormat,
    };

    ImGui_ImplVulkan_InitInfo initInfo{
        .ApiVersion = API_VERSION,
        .Instance = getInstance(),
        .PhysicalDevice = getPhysicalDevice(),
        .Device = getDevice(),
        .QueueFamily = m_queueGCT.familyIndex,
        .Queue = m_queueGCT.queue,
        .DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE * 2,
        .MinImageCount = m_swapchain.getImageCount(),
        .ImageCount = m_swapchain.getImageCount(),
        .PipelineInfoMain = {.PipelineRenderingCreateInfo = pipelineRenderingInfo},
        .UseDynamicRendering = true,
    };

    ImGui_ImplVulkan_Init(&initInfo);

    return VK_SUCCESS;
}

VkResult VulkanApp::setupImguiStyle() {
    // https://github.com/ocornut/imgui/issues/707#issuecomment-4107169777

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // --- 1. Sizing and Spacing ---
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ScrollbarSize = 15.0f;
    style.GrabMinSize = 10.0f;

    // --- 2. Borders & Rounding ---
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    // --- 3. Color Palette ---

    // Text
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.50f, 0.65f, 1.00f);

    // Backgrounds
    colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.09f, 0.12f, 0.80f); // Deep midnight
    colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.12f, 0.16f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.09f, 0.12f, 0.95f);

    // Borders
    colors[ImGuiCol_Border] = ImVec4(0.15f, 0.25f, 0.35f, 0.70f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frames (Inputs, Checkboxes, etc.)
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.18f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.28f, 0.40f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.38f, 0.55f, 1.00f);

    // Title Bars
    colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.22f, 0.35f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.08f, 0.12f, 1.00f);

    // Menus
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.16f, 0.22f, 1.00f);

    // Scrollbars
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.08f, 0.11f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.32f, 0.48f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.42f, 0.60f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.35f, 0.50f, 0.75f, 1.00f);

    // Interactables
    colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.55f, 0.85f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.48f, 0.75f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.60f, 0.90f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.18f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.48f, 0.75f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.60f, 0.90f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.20f, 0.32f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.20f, 0.32f, 1.00f);

    // Tables
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.25f, 0.40f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.20f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.25f, 0.40f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);

    // Misc
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.30f, 0.55f, 0.85f, 0.40f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.50f, 0.80f, 1.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);

    return VK_SUCCESS;
}

VkResult VulkanApp::createFrameResources() {
    const VkFormat formatSRGB = VK_FORMAT_R8G8B8A8_SRGB;
    const VkFormat formatUNORM = VK_FORMAT_R8G8B8A8_UNORM;
    const std::array formats = {formatSRGB, formatUNORM};

    m_frameFormat = formatSRGB;

    const VkImageFormatListCreateInfo formatListInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = formats.size(),
        .pViewFormats = formats.data(),
    };

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &formatListInfo,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = formatSRGB,
        .extent = {m_windowSize.width, m_windowSize.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    const VmaAllocationCreateInfo allocInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };

    VK_CHECK_RETURN(vmaCreateImage(m_allocator, &imageInfo, &allocInfo, &m_frameImage, &m_frameImageAlloc, nullptr));

    VK_CHECK_RETURN(executeSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_frameImage,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        const VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        const VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }));

    VkImageViewCreateInfo imageViewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_frameImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = formatSRGB,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };

    VK_CHECK_RETURN(vkCreateImageView(getDevice(), &imageViewInfo, nullptr, &m_frameImageViewSRGB));

    imageViewInfo.format = formatUNORM;

    VK_CHECK_RETURN(vkCreateImageView(getDevice(), &imageViewInfo, nullptr, &m_frameImageViewUNORM));

    if (!m_headless) {
        m_frameImageDescriptorSet =
            ImGui_ImplVulkan_AddTexture(m_frameImageViewUNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    return VK_SUCCESS;
}

void VulkanApp::destroyFrameResources() {
    if (!m_headless) {
        ImGui_ImplVulkan_RemoveTexture(m_frameImageDescriptorSet);
    }
    vkDestroyImageView(getDevice(), m_frameImageViewSRGB, nullptr);
    vkDestroyImageView(getDevice(), m_frameImageViewUNORM, nullptr);
    vmaDestroyImage(m_allocator, m_frameImage, m_frameImageAlloc);
}

void VulkanApp::setupAudio() {
    if (!m_enableRTX) return;

    m_audioEngine = std::make_unique<ma_engine>();
    m_defaultSound = std::make_unique<ma_sound>();
    m_lpfNode = std::make_unique<ma_lpf_node>();
    m_hpfNode = std::make_unique<ma_hpf_node>();

    if (ma_engine_init(nullptr, m_audioEngine.get()) != MA_SUCCESS) {
        return;
    }

    ma_engine_listener_set_position(m_audioEngine.get(), 0, m_camPos.x, m_camPos.y, m_camPos.z);
    ma_engine_listener_set_direction(m_audioEngine.get(), 0, m_camDir.x, m_camDir.y, m_camDir.z);
    ma_engine_listener_set_world_up(m_audioEngine.get(), 0, 0.f, 1.f, 0.f);

    int x, y, c;
    const uint8_t* d = stbi_load(Fs::findPath("assets/noise.png").string().c_str(), &x, &y, &c, 0);
    if (d == nullptr) return;

    ma_resource_manager* rm = ma_engine_get_resource_manager(m_audioEngine.get());
    ma_resource_manager_register_encoded_data(rm, "audio", d, x * y * c);

    if (ma_sound_init_from_file(m_audioEngine.get(), "audio", 0, nullptr, nullptr, m_defaultSound.get()) !=
        MA_SUCCESS) {
        return;
    }

    ma_sound_set_position(m_defaultSound.get(), -0.16f, 1.24f, -10.16f);
    ma_sound_set_direction(m_defaultSound.get(), 0.45f, 0.05f, -0.88f);
    ma_sound_set_cone(m_defaultSound.get(), 0.2f * glm::pi<float>(), 0.8f * glm::pi<float>(), 0.f);
    ma_sound_set_rolloff(m_defaultSound.get(), 15.f);
    ma_sound_set_looping(m_defaultSound.get(), MA_TRUE);

    const ma_uint32 channels = ma_engine_get_channels(m_audioEngine.get());
    const ma_uint32 sampleRate = ma_engine_get_sample_rate(m_audioEngine.get());
    ma_node_graph* nodeGraph = ma_engine_get_node_graph(m_audioEngine.get());

    const ma_hpf_node_config hpfConfig = ma_hpf_node_config_init(channels, sampleRate, 300.f, 4);
    if (ma_hpf_node_init(nodeGraph, &hpfConfig, nullptr, m_hpfNode.get()) != MA_SUCCESS) {
        return;
    }
    if (ma_node_attach_output_bus(m_defaultSound.get(), 0, m_hpfNode.get(), 0) != MA_SUCCESS) {
        return;
    }

    const ma_lpf_node_config lpfConfig = ma_lpf_node_config_init(channels, sampleRate, 3800.f, 4);
    if (ma_lpf_node_init(nodeGraph, &lpfConfig, nullptr, m_lpfNode.get()) != MA_SUCCESS) {
        return;
    }
    if (ma_node_attach_output_bus(m_hpfNode.get(), 0, m_lpfNode.get(), 0) != MA_SUCCESS) {
        return;
    }

    if (ma_node_attach_output_bus(m_lpfNode.get(), 0, ma_engine_get_endpoint(m_audioEngine.get()), 0) != MA_SUCCESS) {
        return;
    }

    ma_sound_start(m_defaultSound.get());
}

//--------------------------------------------------------------------------------------------------

void VulkanApp::deinit() {
    if (m_frameImage != VK_NULL_HANDLE) {
        destroyFrameResources();
    }

    if (m_headless) {
        ImGui_ImplNull_Shutdown();
    } else {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }
    ImGui::DestroyContext();

    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }

    if (m_commandPool) {
        vkDestroyCommandPool(getDevice(), m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    if (m_transientCommandPool) {
        vkDestroyCommandPool(getDevice(), m_transientCommandPool, nullptr);
        m_transientCommandPool = VK_NULL_HANDLE;
    }

    m_swapchain.deinit();

    if (m_surface) {
        vkDestroySurfaceKHR(getInstance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    m_context.deinit();

    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();
}

//--------------------------------------------------------------------------------------------------

VkResult VulkanApp::run(
    std::function<void()> onDrawUi, std::function<void(VkCommandBuffer)> onRender, std::function<void()> onResize
) {
    if (m_headless) {
        for (uint32_t frame = 0; frame <= 60; frame++) {
            const VkCommandBuffer commandBuffer = m_commandBuffers[0];
            VK_CHECK_RETURN(vkResetCommandBuffer(commandBuffer, 0));

            const VkCommandBufferBeginInfo beginInfo{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            VK_CHECK_RETURN(vkBeginCommandBuffer(commandBuffer, &beginInfo));

            updateCameraMatrices();

            ImGui_ImplNull_NewFrame();
            ImGui::NewFrame();

            buildGui(onDrawUi);
            processInput();
            ImGui::Render();

            onRender(commandBuffer);

            VK_CHECK_RETURN(vkEndCommandBuffer(commandBuffer));

            const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            const VkSubmitInfo submitInfo{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &commandBuffer,
            };

            VK_CHECK_RETURN(vkQueueSubmit(m_queueGCT.queue, 1, &submitInfo, VK_NULL_HANDLE));

            VK_CHECK_RETURN(vkQueueWaitIdle(m_queueGCT.queue));

            if (frame % 10 == 0) {
                std::ostringstream os;
                os << "frame_" << frame << ".png";
                captureScreenshot(os.str(), frame == 60);
            }
        }

        return VK_SUCCESS;
    }

    const VkResult res = [&]() {
        const VkQueue queue = m_queueGCT.queue;

        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();

            {
                int width, height;
                glfwGetFramebufferSize(m_window, &width, &height);

                while (width == 0 || height == 0) {
                    glfwWaitEvents();
                    glfwGetFramebufferSize(m_window, &width, &height);
                }

                const VkExtent2D newSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
                if (newSize.width != m_windowSize.width || newSize.height != m_windowSize.height) {
                    m_swapchain.requestRebuild();
                }
            }

            if (m_swapchain.needsRebuild()) {
                m_swapchain.update(m_windowSize, m_vsync);

                destroyFrameResources();
                createFrameResources();

                onResize();
            }

            updateCameraMatrices();

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            m_cpuTime = std::chrono::high_resolution_clock::now() - m_cpuTimeStart;

            const VkResult res = m_swapchain.acquire();
            if (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR) {
                m_cpuTimeStart = std::chrono::high_resolution_clock::now();

                const VkFence inFlightFence = m_swapchain.getActiveInFlightFence();
                const VkSemaphore acquireSemaphore = m_swapchain.getActiveAcquireSemaphore();
                const VkSemaphore presentSemaphore = m_swapchain.getActivePresentSemaphore();

                const VkCommandBuffer commandBuffer = m_commandBuffers[m_swapchain.getCurrentFrameIdx()];

                VK_CHECK_RETURN(vkResetCommandBuffer(commandBuffer, 0));

                const VkCommandBufferBeginInfo beginInfo{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                };

                VK_CHECK_RETURN(vkBeginCommandBuffer(commandBuffer, &beginInfo));

                buildGui(onDrawUi);
                processInput();
                ImGui::Render();

                onRender(commandBuffer);

                drawFrame(commandBuffer);

                VK_CHECK_RETURN(vkEndCommandBuffer(commandBuffer));

                const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

                const VkSubmitInfo submitInfo{
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .waitSemaphoreCount = 1,
                    .pWaitSemaphores = &acquireSemaphore,
                    .pWaitDstStageMask = &waitStage,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &commandBuffer,
                    .signalSemaphoreCount = 1,
                    .pSignalSemaphores = &presentSemaphore,
                };

                VK_CHECK_RETURN(vkQueueSubmit(queue, 1, &submitInfo, inFlightFence));

                m_swapchain.present();

                if (m_captureScreenshot) {
                    m_captureScreenshot = false;
                    captureScreenshot("screenshot.png");
                }
            }
        }

        return VK_SUCCESS;
    }();

    vkDeviceWaitIdle(getDevice());

    return res;
}

void VulkanApp::updateCameraMatrices() {
    if (m_camIsFirstPerson) {
        m_viewMat = glm::lookAt(m_camPos, m_camPos + m_camDir, glm::vec3(0.0f, 1.0f, 0.0f));
    } else {
        m_camPos = {
            m_camRadius * glm::cos(m_camPitchAngle) * glm::cos(m_camYawAngle),
            m_camRadius * glm::sin(m_camPitchAngle),
            m_camRadius * glm::cos(m_camPitchAngle) * glm::sin(m_camYawAngle),
        };
        m_viewMat = glm::lookAt(m_camPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    const VkExtent2D frameExtent = m_swapchain.getExtent();
    const float aspect = frameExtent.width / (float)frameExtent.height;
    m_projMat = glm::perspective(glm::radians(m_camFovDeg), aspect, 0.1f, m_sceneSize * 2.f);
    m_projMat[1][1] *= -1;

    if (m_audioEngine) {
        ma_engine_listener_set_position(m_audioEngine.get(), 0, m_camPos.x, m_camPos.y, m_camPos.z);
        ma_engine_listener_set_direction(m_audioEngine.get(), 0, m_camDir.x, m_camDir.y, m_camDir.z);
    }
}

void VulkanApp::buildGui(std::function<void()> callback) {
    ImGuiIO& io = ImGui::GetIO();

    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(200.f, io.DisplaySize.y));

        ImGui::Begin("Configuration", nullptr, 0);

        ImGui::Text("Performance");
        ImGui::Separator();

        ImGui::Text("Resolution: %dx%d", m_windowSize.width, m_windowSize.height);
        ImGui::Text("Frame: %.2f ms (%.1f fps)", io.DeltaTime * 1000.f, io.Framerate);
        ImGui::Text("CPU: %.3f ms", m_cpuTime.count());
        ImGui::Text("VSync %s", m_vsync ? "ON" : "OFF");

        ImGui::NewLine();

        if (ImGui::Button("Screenshot (F12)") || ImGui::IsKeyPressed(ImGuiKey_F12)) {
            m_captureScreenshot = true;
        }

        ImGui::NewLine();

        callback();

        ImGui::End();
    }

    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(m_windowSize.width, m_windowSize.height));
        const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("Viewport", nullptr, flags);

        ImGui::Image((ImTextureID)m_frameImageDescriptorSet, ImGui::GetContentRegionAvail());
        m_viewportHovered = ImGui::IsItemHovered();

        ImGui::End();

        ImGui::PopStyleVar();
    }
}

void VulkanApp::processInput() {
    const auto& io = ImGui::GetIO();

    m_mouseDragging = io.MouseDown[ImGuiMouseButton_Left] && (m_mouseDragging || m_viewportHovered);
    if (m_mouseDragging) {
        const float rotateSpeed = 0.005f;
        m_camYawAngle += io.MouseDelta.x * rotateSpeed;
        m_camPitchAngle += io.MouseDelta.y * rotateSpeed;

        const float twoPi = glm::two_pi<float>();
        if (m_camYawAngle < 0) m_camYawAngle += twoPi;
        if (m_camYawAngle >= twoPi) m_camYawAngle -= twoPi;

        const float limit = glm::radians(89.0f);
        if (m_camPitchAngle > limit) m_camPitchAngle = limit;
        if (m_camPitchAngle < -limit) m_camPitchAngle = -limit;
    }

    if (m_camIsFirstPerson) {
        m_camDir = glm::normalize(
            glm::vec3(
                glm::cos(-m_camPitchAngle) * glm::cos(m_camYawAngle),
                glm::sin(-m_camPitchAngle),
                glm::cos(-m_camPitchAngle) * glm::sin(m_camYawAngle)
            )
        );

        const glm::vec3 right = glm::normalize(glm::cross(m_camDir, glm::vec3(0.f, 1.f, 0.f)));
        const glm::vec3 up = glm::cross(right, m_camDir);

        glm::vec3 movement{0.f};

        if (ImGui::IsKeyDown(ImGuiKey_W)) movement += m_camDir;
        if (ImGui::IsKeyDown(ImGuiKey_S)) movement -= m_camDir;
        if (ImGui::IsKeyDown(ImGuiKey_A)) movement -= right;
        if (ImGui::IsKeyDown(ImGuiKey_D)) movement += right;
        if (ImGui::IsKeyDown(ImGuiKey_Space)) movement += up;
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) movement -= up;

        const float speed = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 5.f : 1.f;
        m_camPos += movement * speed * io.DeltaTime;

    } else {
        m_camPos = {
            m_camRadius * glm::cos(m_camPitchAngle) * glm::cos(m_camYawAngle),
            m_camRadius * glm::sin(m_camPitchAngle),
            m_camRadius * glm::cos(m_camPitchAngle) * glm::sin(m_camYawAngle),
        };

        m_camRadius = glm::clamp(m_camRadius - io.MouseWheel * m_sceneSize * 0.01f, 0.1f, m_sceneSize);
    }
}

void VulkanApp::drawFrame(VkCommandBuffer commandBuffer) {
    {
        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_NONE,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_swapchain.getImage(m_swapchain.getCurrentImageIdx()),
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        const auto srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        const auto dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_swapchain.getImageView(m_swapchain.getCurrentImageIdx()),
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {{{0.f, 0.f, 0.f, 1.f}}},
    };

    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, m_windowSize},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRendering(commandBuffer);

    {
        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_NONE,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = m_swapchain.getImage(m_swapchain.getCurrentImageIdx()),
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        const auto srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const auto dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}

void VulkanApp::captureScreenshot(std::string filename, bool wait) {
    VK_CHECK(vkQueueWaitIdle(m_queueGCT.queue));

    const VkExtent3D extent = {m_windowSize.width, m_windowSize.height, 1};
    const VkDeviceSize bufferSize = extent.width * extent.height * 4;

    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    const VmaAllocationCreateInfo allocInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };

    VkBuffer buffer;
    VmaAllocation allocation;
    VK_CHECK(vmaCreateBuffer(getAllocator(), &bufferInfo, &allocInfo, &buffer, &allocation, nullptr));

    VK_CHECK(executeSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
        {
            const VkImageMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = m_frameImage,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            };

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier
            );
        }

        const VkBufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = extent,
        };

        vkCmdCopyImageToBuffer(commandBuffer, m_frameImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

        {
            const VkImageMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = m_frameImage,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            };

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier
            );
        }
    }));

    void* data;
    VK_CHECK(vmaMapMemory(getAllocator(), allocation, &data));
    std::vector<uint8_t> imageData((uint8_t*)data, (uint8_t*)data + bufferSize);
    vmaUnmapMemory(getAllocator(), allocation);

    vmaDestroyBuffer(getAllocator(), buffer, allocation);

    std::thread writer([filename = std::move(filename), extent, imageData = std::move(imageData)]() {
        stbi_write_png(filename.c_str(), extent.width, extent.height, 4, imageData.data(), extent.width * 4);
        fprintf(stdout, "%s written\n", filename.c_str());
    });
    if (wait) {
        writer.join();
    } else {
        writer.detach();
    }
}

//--------------------------------------------------------------------------------------------------

VkResult VulkanApp::executeSingleTimeCommand(std::function<void(VkCommandBuffer)> callback) const {
    VkCommandBuffer commandBuffer;

    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_transientCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VK_CHECK_RETURN(vkAllocateCommandBuffers(getDevice(), &allocInfo, &commandBuffer));

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK_RETURN(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    callback(commandBuffer);

    VK_CHECK_RETURN(vkEndCommandBuffer(commandBuffer));

    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };

    VK_CHECK_RETURN(vkQueueSubmit(m_queueGCT.queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK_RETURN(vkQueueWaitIdle(m_queueGCT.queue));

    vkFreeCommandBuffers(getDevice(), m_transientCommandPool, 1, &commandBuffer);

    return VK_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

void VulkanApp::setCameraAngles(float pitch, float yaw, float radius) {
    m_camIsFirstPerson = false;

    m_camPitchAngle = pitch;
    m_camYawAngle = yaw;
    m_camRadius = radius;

    const float twoPi = glm::two_pi<float>();
    if (m_camYawAngle < 0) m_camYawAngle += twoPi;
    if (m_camYawAngle >= twoPi) m_camYawAngle -= twoPi;

    const float limit = glm::radians(89.0f);
    if (m_camPitchAngle > limit) m_camPitchAngle = limit;
    if (m_camPitchAngle < -limit) m_camPitchAngle = -limit;
}

void VulkanApp::setCameraFirstPerson(const glm::vec3& pos, const glm::vec3& dir) {
    m_camIsFirstPerson = true;

    m_camPos = pos;
    m_camDir = glm::normalize(dir);

    m_camPitchAngle = -glm::asin(m_camDir.y);
    m_camYawAngle = glm::atan(m_camDir.z, m_camDir.x);
}

void VulkanApp::setCameraFov(float fov) {
    m_camFovDeg = fov;
}

void VulkanApp::setSceneSize(float size) {
    m_sceneSize = size;
    m_camRadius = glm::min(m_camRadius, size);
}

float VulkanApp::getDeltaTime() const {
    return ImGui::GetIO().DeltaTime;
}
