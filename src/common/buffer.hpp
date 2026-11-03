#pragma once

#include <volk.h>

#include <vk_mem_alloc.h>

#include "core/check_error.hpp"

#include <cassert>

struct Buffer final {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;

    VkResult init(
        VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags,
        VkDeviceSize minAlignment = 0
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
            .minAlignment = minAlignment,
        };

        VK_CHECK_RETURN(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr));

        return VK_SUCCESS;
    }

    void deinit(VmaAllocator allocator) {
        vmaDestroyBuffer(allocator, buffer, allocation);
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }

    VkDeviceAddress getDeviceAddress(VkDevice device) {
        // Buffer has to be created with 'VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT' usage flag!

        const VkBufferDeviceAddressInfo addressInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer,
        };

        return vkGetBufferDeviceAddress(device, &addressInfo);
    }
};
