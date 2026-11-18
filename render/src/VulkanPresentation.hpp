#pragma once
#include "PresentationWindow.hpp"
#include <volk.h>

namespace render::detail
{
    const char* const* VulkanSurfaceExtensions(PresentationWindow window, uint32_t& count);
    VkSurfaceKHR       CreateVulkanSurface(PresentationWindow window, VkInstance instance);
    void               DestroyVulkanSurface(PresentationWindow window, VkInstance instance, VkSurfaceKHR surface);
} // namespace render::detail
