#pragma once
#include "PresentationWindow.hpp"
#include <SDL3/SDL.h>
#include <string_view>

inline bool InitTestWindowHost()
{
    return SDL_InitSubSystem(SDL_INIT_VIDEO);
}

inline void QuitTestWindowHost()
{
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

inline render::detail::PresentationWindow OpenTestWindow(std::string_view api, int w, int h, const char* title)
{
    return render::detail::PresentationWindow(render::detail::CreateSDLWindow(title, w, h, api == "vulkan", false));
}

inline void CloseTestWindow(render::detail::PresentationWindow window)
{
    SDL_DestroyWindow(window.sdl);
}

inline void PollTestWindow()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
    }
}

inline void TestPixelSize(render::detail::PresentationWindow window, int* w, int* h)
{
    SDL_GetWindowSizeInPixels(window.sdl, w, h);
}

inline void TestPixelDensity(float* x, float* y)
{
    auto* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    *x = *y = mode ? mode->pixel_density : 1;
}
