#pragma once

#include <volk.h>

#include <cassert>
#include <vector>

//--------------------------------------------------------------------------------------------------

struct ExtensionInfo {
    const char* extensionName = nullptr;
    void* feature = nullptr;
};

struct ContextInitInfo {
    const char* applicationName = "Vulkan App";
    uint32_t apiVersion = VK_API_VERSION_1_3;
    std::vector<const char*> instanceExtensions = {};
    std::vector<ExtensionInfo> deviceExtensions = {};
    std::vector<VkQueueFlags> queues = {VK_QUEUE_GRAPHICS_BIT};

#if NDEBUG
    bool enableValidationLayers = false;
    bool verbose = false;
#else
    bool enableValidationLayers = true;
    bool verbose = true;
#endif
    bool extraVerbose = false;
};

struct QueueInfo {
    uint32_t familyIndex = ~0U;     // Family index of the queue (graphic, compute, transfer, ...)
    uint32_t queueIndex = ~0U;      // Index of the queue in the family
    VkQueue queue = VK_NULL_HANDLE; // The queue object
};

//--------------------------------------------------------------------------------------------------

class Context final {
  public:
    Context() = default;
    ~Context() { assert(m_instance == VK_NULL_HANDLE); } // Forgot to call deinit ?

    VkResult init(const ContextInitInfo& info);
    void deinit();

    VkInstance instance() const { return m_instance; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    const QueueInfo& getQueueInfo(uint32_t idx) const { return m_queueInfos[idx]; }

  private:
    VkResult createInstance(const ContextInitInfo& info);
    VkResult selectPhysicalDevice(const ContextInitInfo& info);
    VkResult findQueueFamilies(const ContextInitInfo& info);
    VkResult createDevice(const ContextInitInfo& info);

    VkResult printVulkanVersion();
    VkResult printGpus();
    VkResult printQueues();

  private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    std::vector<QueueInfo> m_queueInfos{};
    std::vector<VkDeviceQueueCreateInfo> m_queueCreateInfos{};
    std::vector<std::vector<float>> m_queuePriorities{};

    VkDebugUtilsMessengerEXT m_dbgMessenger = VK_NULL_HANDLE;
};
