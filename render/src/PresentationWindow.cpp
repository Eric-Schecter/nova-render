#include "PresentationWindow.hpp"
#include <SDL3/SDL.h>
#ifdef NOVA_HAS_VULKAN
#include "VulkanPresentation.hpp"
#include <SDL3/SDL_vulkan.h>
#include <stdexcept>
#if defined(__APPLE__)
#include <string>
#endif
#endif

namespace render::detail
{
    SDL_Window* CreateSDLWindow(const char* title, int width, int height, bool vulkan, bool resizable)
    {
        SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (resizable)
            flags |= SDL_WINDOW_RESIZABLE;
        if (vulkan)
            flags |= SDL_WINDOW_VULKAN;
#ifdef SDL_PLATFORM_APPLE
        else
            flags |= SDL_WINDOW_METAL;
#endif
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

    void* PresentationWindow::NativeHandle() const
    {
        if (sdl)
        {
#ifdef _WIN32
            return SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__ANDROID__)
            return SDL_GetPointerProperty(SDL_GetWindowProperties(sdl), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#endif
        }
        return nullptr;
    }

    void PresentationWindow::CursorPosition(double& x, double& y) const
    {
        float px, py;
        SDL_GetMouseState(&px, &py);
        x = px;
        y = py;
    }

#ifdef NOVA_HAS_VULKAN
    const char* const* VulkanSurfaceExtensions(PresentationWindow, uint32_t& count)
    {
        return SDL_Vulkan_GetInstanceExtensions(&count);
    }

    VkSurfaceKHR CreateVulkanSurface(PresentationWindow window, VkInstance instance)
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window.sdl, instance, nullptr, &surface))
            throw std::runtime_error(SDL_GetError());
        return surface;
    }

    void DestroyVulkanSurface(PresentationWindow, VkInstance instance, VkSurfaceKHR surface)
    {
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    }
#endif
} // namespace render::detail
