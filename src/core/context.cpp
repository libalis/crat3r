#include "context.hpp"

#include "check_error.hpp"
#include "vk_string.hpp"

#include <cstdio>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

//--------------------------------------------------------------------------------------------------

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        const auto severity = [](VkDebugUtilsMessageSeverityFlagBitsEXT s) {
            if (s & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) return "ERROR";
            if (s & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) return "WARNING";
            if (s & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) return "INFO";
            if (s & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) return "VERBOSE";
            return "INVALID";
        };

        fprintf(
            stderr,
            "Validation %s: [ %s ] | MessageID = 0x%x\n%s\n",
            severity(messageSeverity),
            pCallbackData->pMessageIdName,
            pCallbackData->messageIdNumber,
            pCallbackData->pMessage
        );
    }

    return VK_FALSE;
}

//--------------------------------------------------------------------------------------------------

VkResult Context::init(const ContextInitInfo& info) {
    VK_CHECK_RETURN(volkInitialize());

    VK_CHECK_RETURN(createInstance(info));
    VK_CHECK_RETURN(selectPhysicalDevice(info));
    VK_CHECK_RETURN(createDevice(info));

    if (info.verbose) {
        fprintf(stderr, "_________________________________________________\n");
        VK_CHECK_RETURN(printVulkanVersion());

        fprintf(stderr, "_________________________________________________\n");
        VK_CHECK_RETURN(printGpus());

        if (info.extraVerbose) {
            fprintf(stderr, "_________________________________________________\n");
            VK_CHECK_RETURN(printQueues());
        }

        fprintf(stderr, "_________________________________________________\n\n");
    }

    return VK_SUCCESS;
}

VkResult Context::createInstance(const ContextInitInfo& info) {
    const VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = info.applicationName,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = info.apiVersion,
    };

    std::vector<const char*> layers;
    std::vector<const char*> extensions = info.instanceExtensions;

    if (info.enableValidationLayers) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    const VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    VK_CHECK_RETURN(vkCreateInstance(&createInfo, nullptr, &m_instance));

    volkLoadInstance(m_instance);

    if (info.enableValidationLayers) {
        const VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback,
        };

        VK_CHECK_RETURN(vkCreateDebugUtilsMessengerEXT(m_instance, &debugCreateInfo, nullptr, &m_dbgMessenger));
    }

    return VK_SUCCESS;
}

static bool vkVersionAtLeast(uint32_t versionA, uint32_t versionB) {
    const uint32_t aWithoutVariant = versionA - VK_MAKE_API_VERSION(VK_API_VERSION_VARIANT(versionA), 0, 0, 0);
    const uint32_t bWithoutVariant = versionB - VK_MAKE_API_VERSION(VK_API_VERSION_VARIANT(versionB), 0, 0, 0);
    return aWithoutVariant >= bWithoutVariant;
}

VkResult Context::selectPhysicalDevice(const ContextInitInfo& info) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        fprintf(stderr, "%s: Failed to find any GPUs with Vulkan support!\n", __FUNCTION__);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // search for discrete gpu
    m_physicalDevice = devices[0];
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);

        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physicalDevice = device;
            break;
        }
    }

    // check for vulkan version
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);

    if (!vkVersionAtLeast(properties.apiVersion, info.apiVersion)) {
        fprintf(
            stderr,
            "Requested Vulkan version (%d.%d) is higher than available version (%d.%d).\n",
            VK_VERSION_MAJOR(info.apiVersion),
            VK_VERSION_MINOR(info.apiVersion),
            VK_VERSION_MAJOR(properties.apiVersion),
            VK_VERSION_MINOR(properties.apiVersion)
        );
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // find required queues
    VK_CHECK_RETURN(findQueueFamilies(info));

    return VK_SUCCESS;
}

VkResult Context::findQueueFamilies(const ContextInitInfo& info) {
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    std::unordered_map<uint32_t, uint32_t> queueFamilyUsage;
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        queueFamilyUsage[i] = 0;
    }

    const auto findQueue = [&](VkQueueFlags desiredQueue) {
        for (uint32_t j = 0; j < queueFamilyCount; ++j) {
            // Check for an exact match and unused queue family
            // Avoid queue family with VK_QUEUE_GRAPHICS_BIT if not needed
            if ((queueFamilies[j].queueFlags & desiredQueue) == desiredQueue && queueFamilyUsage[j] == 0 &&
                ((desiredQueue & VK_QUEUE_GRAPHICS_BIT) || !(queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT))) {
                m_queueInfos.push_back({j, queueFamilyUsage[j]});
                queueFamilyUsage[j]++;
                return true;
            }
        }

        for (uint32_t j = 0; j < queueFamilyCount; ++j) {
            // Check for an exact match and allow reuse if queue count not exceeded
            // Avoid queue family with VK_QUEUE_GRAPHICS_BIT if not needed
            if ((queueFamilies[j].queueFlags & desiredQueue) == desiredQueue &&
                queueFamilyUsage[j] < queueFamilies[j].queueCount &&
                ((desiredQueue & VK_QUEUE_GRAPHICS_BIT) || !(queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT))) {
                m_queueInfos.push_back({j, queueFamilyUsage[j]});
                queueFamilyUsage[j]++;
                return true;
            }
        }

        for (uint32_t j = 0; j < queueFamilyCount; ++j) {
            // Check for a partial match and allow reuse if queue count not exceeded
            // Avoid queue family with VK_QUEUE_GRAPHICS_BIT if not needed
            if ((queueFamilies[j].queueFlags & desiredQueue) && queueFamilyUsage[j] < queueFamilies[j].queueCount &&
                ((desiredQueue & VK_QUEUE_GRAPHICS_BIT) || !(queueFamilies[j].queueFlags & VK_QUEUE_GRAPHICS_BIT))) {
                m_queueInfos.push_back({j, queueFamilyUsage[j]});
                queueFamilyUsage[j]++;
                return true;
            }
        }

        for (uint32_t j = 0; j < queueFamilyCount; ++j) {
            // Check for a partial match and allow reuse if queue count not exceeded
            if ((queueFamilies[j].queueFlags & desiredQueue) && queueFamilyUsage[j] < queueFamilies[j].queueCount) {
                m_queueInfos.push_back({j, queueFamilyUsage[j]});
                queueFamilyUsage[j]++;
                return true;
            }
        }

        return false;
    };

    for (const auto desiredQueue : info.queues) {
        if (!findQueue(desiredQueue)) {
            fprintf(stderr, "Failed to find a suitable queue family!\n");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    for (const auto& usage : queueFamilyUsage) {
        if (usage.second > 0) {
            m_queuePriorities.emplace_back(usage.second, 1.0f); // Same priority for all queues in a family
            m_queueCreateInfos.push_back(
                VkDeviceQueueCreateInfo{
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .queueFamilyIndex = usage.first,
                    .queueCount = usage.second,
                    .pQueuePriorities = m_queuePriorities.back().data()
                }
            );
        }
    }

    return VK_SUCCESS;
}

static VkResult checkDeviceExtensionSupport(VkPhysicalDevice device, const ContextInitInfo& info) {
    uint32_t extensionCount;
    VK_CHECK_RETURN(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr));

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    VK_CHECK_RETURN(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data()));

    std::set<std::string> requiredExtensions;
    for (const auto& extension : info.deviceExtensions) {
        requiredExtensions.emplace(extension.extensionName);
    }

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    if (!requiredExtensions.empty()) {
        for (const auto& extension : requiredExtensions) {
            fprintf(stderr, "Required extension not available: %s\n", extension.c_str());
        }
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return VK_SUCCESS;
}

VkResult Context::createDevice(const ContextInitInfo& info) {
    VK_CHECK_RETURN(checkDeviceExtensionSupport(m_physicalDevice, info));

    VkPhysicalDeviceVulkan13Features deviceFeatures13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .shaderDemoteToHelperInvocation = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceVulkan12Features deviceFeatures12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &deviceFeatures13,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
        .scalarBlockLayout = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
    };

    VkPhysicalDeviceFeatures2 deviceFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &deviceFeatures12,
        .features = {.shaderInt64 = VK_TRUE},
    };

    VkBaseInStructure* pNextChain = reinterpret_cast<VkBaseInStructure*>(&deviceFeatures13);

    std::vector<const char*> deviceExtensions;
    deviceExtensions.reserve(info.deviceExtensions.size());
    for (const auto& extension : info.deviceExtensions) {
        deviceExtensions.push_back(extension.extensionName);

        if (extension.feature != nullptr) {
            VkBaseInStructure* featureStruct = reinterpret_cast<VkBaseInStructure*>(extension.feature);
            pNextChain->pNext = featureStruct;
            pNextChain = featureStruct;
        }
    }

    const VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &deviceFeatures,
        .queueCreateInfoCount = static_cast<uint32_t>(m_queueCreateInfos.size()),
        .pQueueCreateInfos = m_queueCreateInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
    };

    VK_CHECK_RETURN(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));

    volkLoadDevice(m_device);

    for (auto& queue : m_queueInfos) {
        vkGetDeviceQueue(m_device, queue.familyIndex, queue.queueIndex, &queue.queue);
    }

    return VK_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

void Context::deinit() {
    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_dbgMessenger) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_dbgMessenger, nullptr);
        m_dbgMessenger = VK_NULL_HANDLE;
    }

    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

//--------------------------------------------------------------------------------------------------

VkResult Context::printVulkanVersion() {
    uint32_t version;
    VK_CHECK_RETURN(vkEnumerateInstanceVersion(&version));

    fprintf(
        stderr,
        "Vulkan Version:  %d.%d.%d\n",
        VK_VERSION_MAJOR(version),
        VK_VERSION_MINOR(version),
        VK_VERSION_PATCH(version)
    );

    return VK_SUCCESS;
}

static std::string getVendorName(uint32_t vendorID) {
    static const std::unordered_map<uint32_t, std::string> vendorMap = {
        {0x1002, "AMD"},
        {0x1010, "ImgTec"},
        {0x10DE, "NVIDIA"},
        {0x13B5, "ARM"},
        {0x5143, "Qualcomm"},
        {0x8086, "INTEL"}
    };

    auto it = vendorMap.find(vendorID);
    return it != vendorMap.end() ? it->second : "Unknown Vendor";
}

static std::string getDeviceType(uint32_t deviceType) {
    static const std::unordered_map<uint32_t, std::string> deviceTypeMap = {
        {VK_PHYSICAL_DEVICE_TYPE_OTHER, "Other"},
        {VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, "Integrated GPU"},
        {VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, "Discrete GPU"},
        {VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU, "Virtual GPU"},
        {VK_PHYSICAL_DEVICE_TYPE_CPU, "CPU"}
    };

    auto it = deviceTypeMap.find(deviceType);
    return it != deviceTypeMap.end() ? it->second : "Unknown";
}

static std::string getVersionString(uint32_t version) {
    return std::to_string(VK_VERSION_MAJOR(version)) + "."   //
           + std::to_string(VK_VERSION_MINOR(version)) + "." //
           + std::to_string(VK_VERSION_PATCH(version));
}

static void printPhysicalDeviceProperties(const VkPhysicalDeviceProperties& properties) {
    fprintf(stderr, " - Device Name    : %s\n", properties.deviceName);
    fprintf(stderr, " - Vendor         : %s\n", getVendorName(properties.vendorID).c_str());
    fprintf(stderr, " - Driver Version : %s\n", getVersionString(properties.driverVersion).c_str());
    fprintf(stderr, " - API Version    : %s\n", getVersionString(properties.apiVersion).c_str());
    fprintf(stderr, " - Device Type    : %s\n", getDeviceType(properties.deviceType).c_str());
}

VkResult Context::printGpus() {
    uint32_t deviceCount = 0;
    VK_CHECK_RETURN(vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr));

    std::vector<VkPhysicalDevice> gpus(deviceCount);
    VK_CHECK_RETURN(vkEnumeratePhysicalDevices(m_instance, &deviceCount, gpus.data()));

    fprintf(stderr, "Available GPUs: %d\n", deviceCount);

    uint32_t usedGpuIndex = 0;
    for (uint32_t d = 0; d < deviceCount; d++) {
        if (gpus[d] == m_physicalDevice) usedGpuIndex = d;

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(gpus[d], &properties);

        fprintf(stderr, " - %d) %s\n", d, properties.deviceName);
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        fprintf(stderr, "No compatible GPU\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    fprintf(stderr, "Using GPU %u:\n", usedGpuIndex);

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    printPhysicalDeviceProperties(properties);

    return VK_SUCCESS;
}

VkResult Context::printQueues() {
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    fprintf(stderr, "Available Queue Families: %d\n", queueFamilyCount);

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        const auto flags = string_VkQueueFlags(queueFamilies[i].queueFlags);
        fprintf(stderr, " - %d) %2d queues : %s\n", i, queueFamilies[i].queueCount, flags.c_str());
    }

    fprintf(stderr, "Used queues:\n");

    for (const auto& queue : m_queueInfos) {
        fprintf(stderr, " - family: %d, index: %d\n", queue.familyIndex, queue.queueIndex);
    }

    return VK_SUCCESS;
}
