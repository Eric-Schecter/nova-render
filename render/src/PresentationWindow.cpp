#include "PresentationWindow.hpp"
#ifdef NOVA_HAS_VULKAN
#include "VulkanPresentation.hpp"
#endif
#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#if defined(__APPLE__) || defined(_WIN32)
#include <GLFW/glfw3native.h>
#endif
#ifdef NOVA_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace render::detail
{
    void* PresentationWindow::NativeHandle() const
    {
#ifdef NOVA_HAS_SDL3
        if (sdl)
        {
#if defined(__APPLE__)
            return SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(_WIN32)
            return SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#endif
        }
#endif
#ifdef __APPLE__
        return glfwGetCocoaWindow(glfw);
#elif defined(_WIN32)
        return glfwGetWin32Window(glfw);
#else
        return nullptr;
#endif
    }

    void PresentationWindow::CursorPosition(double& x, double& y) const
    {
#ifdef NOVA_HAS_SDL3
        if (sdl)
        {
            float px, py;
            SDL_GetMouseState(&px, &py);
            x = px;
            y = py;
            return;
        }
#endif
        glfwGetCursorPos(glfw, &x, &y);
    }
} // namespace render::detail

#ifdef NOVA_HAS_VULKAN
#include <stdexcept>
#ifdef NOVA_HAS_SDL3
#include <SDL3/SDL_vulkan.h>
#endif
namespace render::detail
{
    const char* const* VulkanSurfaceExtensions(PresentationWindow window, uint32_t& count)
    {
#ifdef NOVA_HAS_SDL3
        if (window.sdl)
            return SDL_Vulkan_GetInstanceExtensions(&count);
#else
        (void)window;
#endif
        return glfwGetRequiredInstanceExtensions(&count);
    }

    VkSurfaceKHR CreateVulkanSurface(PresentationWindow window, VkInstance instance)
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
#ifdef NOVA_HAS_SDL3
        if (window.sdl)
        {
            if (!SDL_Vulkan_CreateSurface(window.sdl, instance, nullptr, &surface))
                throw std::runtime_error(SDL_GetError());
            return surface;
        }
#endif
        if (glfwCreateWindowSurface(instance, window.glfw, nullptr, &surface) != VK_SUCCESS)
            throw std::runtime_error("GLFW could not create Vulkan window surface");
        return surface;
    }

    void DestroyVulkanSurface(PresentationWindow window, VkInstance instance, VkSurfaceKHR surface)
    {
#ifdef NOVA_HAS_SDL3
        if (window.sdl)
        {
            SDL_Vulkan_DestroySurface(instance, surface, nullptr);
            return;
        }
#else
        (void)window;
#endif
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
} // namespace render::detail
#endif
