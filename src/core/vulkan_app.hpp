#pragma once

#include <volk.h>

#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <miniaudio.h>

#include "context.hpp"
#include "swapchain.hpp"

#include <cassert>
#include <chrono>
#include <functional>
#include <memory>

class VulkanApp final {
  public:
    VulkanApp() = default;
    ~VulkanApp() { assert(m_window == nullptr); } // Forgot to call deinit ?

    VkResult init(const char* title, uint32_t windowW, uint32_t windowH, bool vsync = true);
    void deinit();
    void enableRTX();

    VkResult run(
        std::function<void()> onDrawUi, std::function<void(VkCommandBuffer)> onRender, std::function<void()> onResize
    );

    VkResult executeSingleTimeCommand(std::function<void(VkCommandBuffer)> callback) const;

    VkInstance getInstance() const { return m_context.instance(); }
    VkPhysicalDevice getPhysicalDevice() const { return m_context.physicalDevice(); }
    VkDevice getDevice() const { return m_context.device(); }
    const QueueInfo& getQueueInfo(uint32_t idx) const { return m_context.getQueueInfo(idx); }
    const QueueInfo& queueGCT() const { return m_queueGCT; }
    const Swapchain& getSwapchain() const { return m_swapchain; }
    VkCommandPool getCommandPool() const { return m_commandPool; }
    VmaAllocator getAllocator() const { return m_allocator; }

    VkImageView getRenderTargetImageView() const { return m_frameImageViewSRGB; }
    VkFormat getRenderTargetFormat() const { return m_frameFormat; }
    VkExtent2D getRenderTargetExtent() const { return m_windowSize; }

    const glm::mat4& getViewMat() const { return m_viewMat; }
    const glm::mat4& getProjMat() const { return m_projMat; }
    const glm::vec3& getCameraPos() const { return m_camPos; }
    void setCameraAngles(float pitch, float yaw, float radius);
    void setCameraFirstPerson(const glm::vec3& pos, const glm::vec3& dir);
    void setCameraFov(float fov);
    void setSceneSize(float size);

    float getDeltaTime() const;

  private:
    VkResult createContext();
    VkResult createWindow();
    VkResult createSwapchain();
    VkResult createFramebuffers();
    VkResult createCommandPools();
    VkResult createCommandBuffers();
    VkResult createAllocator();

    VkResult setupImgui();
    VkResult setupImguiStyle();
    VkResult createFrameResources();
    void destroyFrameResources();
    void setupAudio();

    void updateCameraMatrices();
    void buildGui(std::function<void()> callback);
    void processInput();
    void drawFrame(VkCommandBuffer commandBuffer);
    void captureScreenshot(std::string filename, bool wait = false);

    static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

  private:
    Context m_context{};
    bool m_enableRTX = false;
    bool m_headless = false;

    GLFWwindow* m_window = nullptr;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkExtent2D m_windowSize{};
    const char* m_windowTitle{};

    Swapchain m_swapchain{};
    bool m_vsync = false;

    QueueInfo m_queueGCT{};

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers{};
    VkCommandPool m_transientCommandPool = VK_NULL_HANDLE;

    VmaAllocator m_allocator = VK_NULL_HANDLE;

    VkImage m_frameImage = VK_NULL_HANDLE;
    VmaAllocation m_frameImageAlloc = VK_NULL_HANDLE;
    VkImageView m_frameImageViewUNORM = VK_NULL_HANDLE;
    VkImageView m_frameImageViewSRGB = VK_NULL_HANDLE;
    VkFormat m_frameFormat = VK_FORMAT_UNDEFINED;
    VkDescriptorSet m_frameImageDescriptorSet = VK_NULL_HANDLE;

    std::unique_ptr<ma_engine> m_audioEngine{};
    std::unique_ptr<ma_sound> m_defaultSound{};
    std::unique_ptr<ma_hpf_node> m_hpfNode{};
    std::unique_ptr<ma_lpf_node> m_lpfNode{};

    std::chrono::time_point<std::chrono::high_resolution_clock> m_cpuTimeStart{};
    std::chrono::duration<double, std::milli> m_cpuTime{};

    bool m_captureScreenshot = false;

    bool m_viewportHovered = false;
    bool m_mouseDragging = false;
    bool m_camIsFirstPerson = false;
    float m_camPitchAngle = 0.f;
    float m_camYawAngle = 0.f;
    float m_camRadius = 4.f;
    float m_camFovDeg = 45.f;
    float m_sceneSize = 10.f;
    glm::vec3 m_camPos{};
    glm::vec3 m_camDir{};
    glm::mat4 m_viewMat{};
    glm::mat4 m_projMat{};
};
