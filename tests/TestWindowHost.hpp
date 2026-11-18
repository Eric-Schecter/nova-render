#pragma once
#include "PresentationWindow.hpp"
#include <string_view>
#ifdef NOVA_HAS_SDL3
#include <SDL3/SDL.h>
#else
#include <GLFW/glfw3.h>
#endif

inline bool InitTestWindowHost()
{
#ifdef NOVA_HAS_SDL3
    return SDL_InitSubSystem(SDL_INIT_VIDEO);
#else
    if (!glfwInit())
        return false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    return true;
#endif
}

inline void QuitTestWindowHost()
{
#ifdef NOVA_HAS_SDL3
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
#else
    glfwTerminate();
#endif
}

inline render::detail::PresentationWindow OpenTestWindow(std::string_view api, int w, int h, const char* title)
{
#ifdef NOVA_HAS_SDL3
    return render::detail::PresentationWindow(render::detail::CreateSDLWindow(title, w, h, api == "vulkan", false));
#else
    (void)api;
    return glfwCreateWindow(w, h, title, nullptr, nullptr);
#endif
}

inline void CloseTestWindow(render::detail::PresentationWindow window)
{
#ifdef NOVA_HAS_SDL3
    SDL_DestroyWindow(window.sdl);
#else
    glfwDestroyWindow(window.glfw);
#endif
}

inline void PollTestWindow()
{
#ifdef NOVA_HAS_SDL3
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
    }
#else
    glfwPollEvents();
#endif
}

inline void TestPixelSize(render::detail::PresentationWindow window, int* w, int* h)
{
#ifdef NOVA_HAS_SDL3
    SDL_GetWindowSizeInPixels(window.sdl, w, h);
#else
    glfwGetFramebufferSize(window.glfw, w, h);
#endif
}

inline void TestPixelDensity(float* x, float* y)
{
#ifdef NOVA_HAS_SDL3
    auto* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    *x = *y = mode ? mode->pixel_density : 1;
#else
    glfwGetMonitorContentScale(glfwGetPrimaryMonitor(), x, y);
#endif
}
