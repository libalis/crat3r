#pragma once

#include <volk.h>

#include <cassert>
#include <vector>

//--------------------------------------------------------------------------------------------------

struct SwapchainInitInfo {
    VkDevice device;
    VkPhysicalDevice physcialDevice;
    VkQueue queue;
    uint32_t queueFamilyIndex;
    VkSurfaceKHR surface;
    VkSurfaceFormatKHR preferredFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    uint32_t preferredImageCount = 3;
    uint32_t preferredFramesInFlight = 2;

    bool headless = false;
};

//--------------------------------------------------------------------------------------------------

class Swapchain final {
  public:
    Swapchain() = default;
    ~Swapchain() { assert(m_swapchain == VK_NULL_HANDLE); } // Forgot to call deinit ?

    VkResult init(const SwapchainInitInfo& info);
    void deinit();

    VkResult update(VkExtent2D& extent, bool vSync);
    VkResult acquire();
    VkResult present();

    uint32_t getCurrentImageIdx() const { return m_currentImage; }
    uint32_t getCurrentFrameIdx() const { return m_currentFrame; }

    VkFence getActiveInFlightFence() const { return m_frameResources[m_currentFrame].inFlightFence; }
    VkSemaphore getActiveAcquireSemaphore() const { return m_frameResources[m_currentFrame].acquireSemaphore; }
    VkSemaphore getActivePresentSemaphore() const { return m_images[m_currentImage].presentSemaphore; }

    uint32_t getImageCount() const { return m_imageCount; }
    uint32_t getMaxFramesInFlight() const { return m_framesInFlight; }

    VkImage getImage(uint32_t i) const { return m_images[i].image; }
    VkImageView getImageView(uint32_t i) const { return m_images[i].imageView; }
    VkFormat getFormat() const { return m_surfaceFormat.format; }
    VkExtent2D getExtent() const { return m_extent; }

    void requestRebuild() { m_needsRebuild = true; }
    bool needsRebuild() const { return m_needsRebuild; }

  private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSemaphore presentSemaphore = VK_NULL_HANDLE;
    };

    struct FrameResources {
        VkFence inFlightFence = VK_NULL_HANDLE;
        VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
    };

  private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSurfaceFormatKHR m_surfaceFormat{};
    VkSurfaceFormatKHR m_preferredSurfaceFormat{};
    VkImageUsageFlags m_imageUsage{};
    VkExtent2D m_extent{};
    bool m_vsync = false;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSwapchainKHR m_oldSwapchain = VK_NULL_HANDLE;
    bool m_needsRebuild = false;

    std::vector<Image> m_images{};
    std::vector<FrameResources> m_frameResources{};

    uint32_t m_preferredImageCount = 0;
    uint32_t m_preferredFramesInFlight = 0;

    uint32_t m_imageCount = 0;
    uint32_t m_framesInFlight = 0;

    uint32_t m_currentImage = 0;
    uint32_t m_currentFrame = 0;

    bool m_headless = false;
};
