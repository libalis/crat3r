#include "swapchain.hpp"

#include "check_error.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>

//--------------------------------------------------------------------------------------------------

VkResult Swapchain::init(const SwapchainInitInfo& info) {
    m_device = info.device;
    m_physicalDevice = info.physcialDevice;
    m_queue = info.queue;
    m_queueFamilyIndex = info.queueFamilyIndex;
    m_surface = info.surface;
    m_preferredSurfaceFormat = info.preferredFormat;
    m_imageUsage = info.imageUsage;
    m_preferredImageCount = info.preferredImageCount;
    m_preferredFramesInFlight = info.preferredFramesInFlight;
    m_headless = info.headless;

    if (m_headless) {
        return VK_SUCCESS;
    }

    VkBool32 supportsPresent = VK_FALSE;
    VK_CHECK_RETURN(
        vkGetPhysicalDeviceSurfaceSupportKHR(info.physcialDevice, info.queueFamilyIndex, info.surface, &supportsPresent)
    );

    if (!supportsPresent) {
        fprintf(
            stderr,
            "Selected queue family %d cannot present on surface %px. Swapchain creation failed.\n",
            info.queueFamilyIndex,
            info.surface
        );
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return VK_SUCCESS;
}

void Swapchain::deinit() {
    for (auto& image : m_images) {
        vkDestroyImageView(m_device, image.imageView, nullptr);
        vkDestroySemaphore(m_device, image.presentSemaphore, nullptr);
    }

    for (auto& frame : m_frameResources) {
        vkDestroySemaphore(m_device, frame.acquireSemaphore, nullptr);
        vkDestroyFence(m_device, frame.inFlightFence, nullptr);
    }

    m_images.clear();
    m_frameResources.clear();

    if (m_swapchain) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

//--------------------------------------------------------------------------------------------------

static uint32_t chooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t preferredImageCount) {
    const uint32_t minImageCount = capabilities.minImageCount;
    const uint32_t maxImageCount =
        (capabilities.maxImageCount != 0) ? capabilities.maxImageCount : std::numeric_limits<uint32_t>::max();

    return std::clamp(minImageCount, preferredImageCount, maxImageCount);
}

static VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D fallbackExtent) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    return VkExtent2D{
        .width = std::clamp(
            fallbackExtent.width, //
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width
        ),
        .height = std::clamp(
            fallbackExtent.height, //
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height
        ),
    };
}

static VkSurfaceFormatKHR selectSurfaceFormat(
    VkSurfaceFormatKHR preferredFormat, const std::vector<VkSurfaceFormatKHR>& availableFormats
) {
    // return default if surface has no preferred format
    if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED) {
        return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }

    // try to match preferred format exactly
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == preferredFormat.format &&
            availableFormat.colorSpace == preferredFormat.colorSpace) {
            return availableFormat;
        }
    }

    // try to match preferred format without color space
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == preferredFormat.format) {
            return availableFormat;
        }
    }

    // fall back to first format
    return availableFormats[0];
}

static VkPresentModeKHR selectPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes, bool vSync) {
    if (!vSync) {
        bool mailboxSupported = false;
        bool immediateSupported = false;

        for (auto mode : availablePresentModes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                mailboxSupported = true;
            }
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                immediateSupported = true;
            }
        }

        if (immediateSupported) {
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        }

        if (mailboxSupported) {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkResult Swapchain::update(VkExtent2D& extent, bool vsync) {
    if (m_headless) {
        m_imageCount = 1;
        m_framesInFlight = 1;
        m_extent = extent;

        return VK_SUCCESS;
    }

    VK_CHECK_RETURN(vkDeviceWaitIdle(m_device));

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    VK_CHECK_RETURN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCapabilities));

    uint32_t formatCount;
    VK_CHECK_RETURN(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    VK_CHECK_RETURN(
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, surfaceFormats.data())
    );

    uint32_t presentModeCount;
    VK_CHECK_RETURN(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr));
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    VK_CHECK_RETURN(
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data())
    );

    m_imageCount = chooseImageCount(surfaceCapabilities, m_preferredImageCount);
    m_framesInFlight = std::min(m_preferredFramesInFlight, m_imageCount);

    m_extent = chooseSwapExtent(surfaceCapabilities, extent);
    assert(m_extent.width && m_extent.height);
    extent = m_extent;

    m_surfaceFormat = selectSurfaceFormat(m_preferredSurfaceFormat, surfaceFormats);

    const VkPresentModeKHR presentMode = selectPresentMode(presentModes, vsync);
    m_vsync = vsync;

    const VkSurfaceTransformFlagBitsKHR preTransform =
        (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
            : surfaceCapabilities.currentTransform;

    m_oldSwapchain = m_swapchain;

    const VkSwapchainCreateInfoKHR swapchainCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_surface,
        .minImageCount = m_imageCount,
        .imageFormat = m_surfaceFormat.format,
        .imageColorSpace = m_surfaceFormat.colorSpace,
        .imageExtent = m_extent,
        .imageArrayLayers = 1,
        .imageUsage = m_imageUsage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &m_queueFamilyIndex,
        .preTransform = preTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = m_oldSwapchain,
    };

    VK_CHECK_RETURN(vkCreateSwapchainKHR(m_device, &swapchainCreateInfo, nullptr, &m_swapchain));

    // if this is a rebuild, destroy old resources
    if (m_oldSwapchain != VK_NULL_HANDLE) {
        std::swap(m_swapchain, m_oldSwapchain);
        deinit();
        std::swap(m_swapchain, m_oldSwapchain);
    }

    VK_CHECK_RETURN(vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_imageCount, nullptr));
    std::vector<VkImage> images(m_imageCount);
    VK_CHECK_RETURN(vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_imageCount, images.data()));

    VkImageViewCreateInfo imageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = m_surfaceFormat.format,
        .components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A},
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    const VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    m_images.resize(m_imageCount);
    for (int32_t i = 0; i < m_imageCount; i++) {
        m_images[i].image = images[i];

        imageViewCreateInfo.image = images[i];
        VK_CHECK_RETURN(vkCreateImageView(m_device, &imageViewCreateInfo, nullptr, &m_images[i].imageView));

        VK_CHECK_RETURN(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_images[i].presentSemaphore));
    }

    const VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    m_frameResources.resize(m_framesInFlight);
    for (size_t i = 0; i < m_framesInFlight; ++i) {
        VK_CHECK_RETURN(vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameResources[i].inFlightFence));
        VK_CHECK_RETURN(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_frameResources[i].acquireSemaphore));
    }

    m_currentImage = 0;
    m_currentFrame = 0;
    m_needsRebuild = false;

    return VK_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

VkResult Swapchain::acquire() {
    const VkFence inFlightFence = getActiveInFlightFence();
    vkWaitForFences(m_device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

    const VkSemaphore semaphore = getActiveAcquireSemaphore();

    const VkResult res = vkAcquireNextImageKHR(
        m_device, //
        m_swapchain,
        UINT64_MAX,
        semaphore,
        VK_NULL_HANDLE,
        &m_currentImage
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        m_needsRebuild = true;
    } else {
        vkResetFences(m_device, 1, &inFlightFence);
    }

    return res;
}

VkResult Swapchain::present() {
    const VkSemaphore waitSemaphore = getActivePresentSemaphore();

    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &waitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &m_swapchain,
        .pImageIndices = &m_currentImage,
    };

    const VkResult res = vkQueuePresentKHR(m_queue, &presentInfo);

    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        m_needsRebuild = true;
    }

    m_currentFrame = (m_currentFrame + 1) % m_framesInFlight;

    return VK_SUCCESS;
}
