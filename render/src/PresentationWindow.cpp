#include "PresentationWindow.hpp"
#ifdef NOVA_HAS_VULKAN
#include "VulkanPresentation.hpp"
#endif
#ifdef NOVA_HAS_GLFW
#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#if defined(__APPLE__) || defined(_WIN32)
#include <GLFW/glfw3native.h>
#endif
#endif
#ifdef NOVA_HAS_SDL3
#include <SDL3/SDL.h>
#if defined(__APPLE__) && defined(NOVA_HAS_VULKAN)
#include <SDL3/SDL_vulkan.h>
#include <string>
#endif
#endif

namespace render::detail
{
#ifdef NOVA_HAS_SDL3
    SDL_Window* CreateSDLWindow(const char* title, int width, int height, bool vulkan, bool resizable)
    {
        SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (resizable)
            flags |= SDL_WINDOW_RESIZABLE;
        if (vulkan)
            flags |= SDL_WINDOW_VULKAN;
#if defined(__APPLE__) && defined(NOVA_HAS_VULKAN)
        // Match volk's fallback to the standard macOS Vulkan SDK loader location.
        const bool loaderReference =
          vulkan && !SDL_GetHint(SDL_HINT_VULKAN_LIBRARY)
          && (SDL_Vulkan_LoadLibrary(nullptr) || SDL_Vulkan_LoadLibrary("/usr/local/lib/libvulkan.dylib"));
#endif
        SDL_Window* window = SDL_CreateWindow(title, width, height, flags);
#if defined(__APPLE__) && defined(NOVA_HAS_VULKAN)
        if (loaderReference)
        {
            const std::string error = window ? "" : SDL_GetError();
            SDL_Vulkan_UnloadLibrary();
            if (!window)
                SDL_SetError("%s", error.c_str());
        }
#endif
        return window;
    }
#endif

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
#if defined(NOVA_HAS_GLFW) && defined(__APPLE__)
        return glfwGetCocoaWindow(glfw);
#elif defined(NOVA_HAS_GLFW) && defined(_WIN32)
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
#ifdef NOVA_HAS_GLFW
        glfwGetCursorPos(glfw, &x, &y);
#else
        x = y = 0;
#endif
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
#ifdef NOVA_HAS_GLFW
        return glfwGetRequiredInstanceExtensions(&count);
#else
        count = 0;
        return nullptr;
#endif
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
#ifdef NOVA_HAS_GLFW
        if (glfwCreateWindowSurface(instance, window.glfw, nullptr, &surface) != VK_SUCCESS)
            throw std::runtime_error("GLFW could not create Vulkan window surface");
        return surface;
#else
        throw std::runtime_error("Vulkan requires an SDL3 window");
#endif
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
