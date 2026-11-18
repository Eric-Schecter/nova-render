#pragma once

struct GLFWwindow;
struct SDL_Window;

namespace render::detail
{
    SDL_Window* CreateSDLWindow(const char* title, int width, int height, bool vulkan, bool resizable);

    // Borrowed window handle. The host owns the window and outlives the renderer.
    struct PresentationWindow
    {
        GLFWwindow* glfw = nullptr;
        SDL_Window* sdl  = nullptr;

        PresentationWindow() = default;
        PresentationWindow(GLFWwindow* value)
          : glfw(value)
        {
        }
        explicit PresentationWindow(SDL_Window* value)
          : sdl(value)
        {
        }
        explicit operator bool() const { return glfw || sdl; }
        void*    NativeHandle() const;
        void     CursorPosition(double& x, double& y) const;
    };
} // namespace render::detail
