#pragma once

#include <vulkan/vulkan_core.h>

#include "vk_string.hpp"

#include <cstdio>

//--------------------------------------------------------------------------------------------------

inline void reportError(VkResult result, const char* expression, const char* file, int line) {
    const char* errMsg = string_VkResult(result);
    fprintf(stderr, "Vulkan error: %s from %s, %s:%d\n", errMsg, expression, file, line);
}

#define VK_CHECK(vkFnc)                                                                                                \
    {                                                                                                                  \
        const VkResult checkResult = (vkFnc);                                                                          \
        if (checkResult < 0) {                                                                                         \
            reportError(checkResult, #vkFnc, __FILE__, __LINE__);                                                      \
            exit(1);                                                                                                   \
        }                                                                                                              \
    }

#define VK_CHECK_RETURN(vkFnc)                                                                                         \
    {                                                                                                                  \
        const VkResult checkResult = (vkFnc);                                                                          \
        if (checkResult < 0) {                                                                                         \
            reportError(checkResult, #vkFnc, __FILE__, __LINE__);                                                      \
            return checkResult;                                                                                        \
        }                                                                                                              \
    }
