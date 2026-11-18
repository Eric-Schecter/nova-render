#include <SDL3/SDL.h>
#include <imgui.h>
#include <iostream>
#include <render/Window.hpp>
#include <stdexcept>
#include <string_view>

namespace
{
    void require(bool okay, const char* message)
    {
        if (!okay)
            throw std::runtime_error(message);
    }

    void push(SDL_Event& event)
    {
        require(SDL_PushEvent(&event), SDL_GetError());
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Specify metal, vulkan or dx12");
        render::WindowConfig config;
        config.width                     = 480;
        config.height                    = 320;
        config.renderer.api              = std::string_view(argv[1]) == "metal"  ? render::GraphicsAPI::Metal
                                           : std::string_view(argv[1]) == "dx12" ? render::GraphicsAPI::D3D12
                                                                                 : render::GraphicsAPI::Vulkan;
        config.renderer.enableValidation = true;
        render::Window window(config);
        window.EnableOrbitControls();
        int          count   = 0;
        SDL_Window** windows = SDL_GetWindows(&count);
        auto*        host    = count == 1 && windows ? windows[0] : nullptr;
        SDL_free(windows);
        require(host != nullptr, "Expected one SDL3 window");
        const auto id         = SDL_GetWindowID(host);
        auto       verifySize = [&] {
            int w, h, pixelsW, pixelsH;
            require(SDL_GetWindowSize(host, &w, &h), SDL_GetError());
            require(SDL_GetWindowSizeInPixels(host, &pixelsW, &pixelsH), SDL_GetError());
            require(
              window.GetFramebufferSize() == std::array<uint32_t, 2>{uint32_t(pixelsW), uint32_t(pixelsH)},
              "Nova framebuffer disagrees with SDL pixel size");
            const auto density = SDL_GetWindowPixelDensity(host);
            require(density >= 1 && pixelsW >= w && pixelsH >= h, "Invalid pixel density");
            std::cout << w << 'x' << h << " logical, " << pixelsW << 'x' << pixelsH << " framebuffer, density "
                      << density << '\n';
        };
        verifySize();
        require(window.PollEvents(), "Window closed before rendering");
        require(window.DrawFrame() == render::FrameResult::Rendered, "Triangle did not render");

        const auto firstSize = window.GetFramebufferSize();
        require(SDL_SetWindowSize(host, 640, 360) && SDL_SyncWindow(host), SDL_GetError());
        require(window.PollEvents(), "Window closed during resize");
        verifySize();
        require(window.GetFramebufferSize() != firstSize, "Framebuffer did not resize");
        require(window.DrawFrame() == render::FrameResult::Rendered, "Triangle did not render after resize");

        const auto before = window.GetCamera();
        SDL_Event  press{};
        press.type            = SDL_EVENT_MOUSE_BUTTON_DOWN;
        press.button.windowID = id;
        press.button.button   = SDL_BUTTON_LEFT;
        push(press);
        SDL_Event move{};
        move.type            = SDL_EVENT_MOUSE_MOTION;
        move.motion.windowID = id;
        move.motion.xrel     = 25;
        move.motion.yrel     = 8;
        push(move);
        press.type = SDL_EVENT_MOUSE_BUTTON_UP;
        push(press);
        SDL_Event wheel{};
        wheel.type           = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.windowID = id;
        wheel.wheel.y        = 1;
        push(wheel);
        require(window.PollEvents(), "Window closed during mouse input");
        auto after = window.GetCamera();
        require(
          after.yaw != before.yaw && after.pitch != before.pitch && after.distance < before.distance,
          "SDL mouse drag and wheel did not reach orbit controls");

        unsigned                     panelFrames = 0;
        float                        panelWheel  = 0;
        render::ParameterPanelConfig panel;
        panel.drawUI = [&] {
            const auto& io = ImGui::GetIO();
            require(
              io.BackendPlatformName && std::string_view(io.BackendPlatformName) == "imgui_impl_sdl3",
              "Wrong ImGui platform backend");
            require(io.DisplayFramebufferScale.x == SDL_GetWindowPixelDensity(host), "Wrong panel pixel scale");
            panelWheel += io.MouseWheel;
            ++panelFrames;
        };
        require(window.EnableParameterPanel(panel), "Parameter panel unavailable");
        require(window.DrawFrame() == render::FrameResult::Rendered && panelFrames == 1, "Panel did not draw");
        push(wheel);
        require(window.PollEvents(), "Window closed during panel input");
        require(
          window.DrawFrame() == render::FrameResult::Rendered && panelWheel > 0, "SDL mouse wheel did not reach ImGui");
        window.WaitIdle();
        require(!window.ValidationErrorCount(), "GPU validation errors");
        std::cout << "SDL3 triangle, resize, mouse orbit and ImGui passed on " << argv[1] << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
