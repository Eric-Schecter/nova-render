#include <SDL3/SDL.h>
#include <imgui.h>
#include <cmath>
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
        window.SetSize(640, 360);
        require(SDL_SyncWindow(host), SDL_GetError());
        require(window.PollEvents(), "Window closed during resize");
        verifySize();
        require(window.GetFramebufferSize() != firstSize, "Framebuffer did not resize");
        require(window.DrawFrame() == render::FrameResult::Rendered, "Triangle did not render after resize");

#if defined(_WIN32) || defined(__APPLE__)
        const auto waitForMinimized = [&](bool expected) {
            const auto deadline = SDL_GetTicks() + 5000;
            while (bool(SDL_GetWindowFlags(host) & SDL_WINDOW_MINIMIZED) != expected)
            {
                require(window.PollEvents(), "Window closed during minimize or restore");
                require(SDL_GetTicks() < deadline, "Window did not minimize or restore");
                SDL_Delay(10);
            }
        };
        require(SDL_MinimizeWindow(host), SDL_GetError());
        waitForMinimized(true);
        require(window.DrawFrame() == render::FrameResult::Skipped, "Minimized frame was not skipped");
        require(SDL_RestoreWindow(host), SDL_GetError());
        waitForMinimized(false);
        require(window.DrawFrame() == render::FrameResult::Rendered, "Triangle did not render after restore");
#endif

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

        render::OrbitCamera seed{44, 12, 5.8f, .85f, {0, .63f, 0}};
        auto                view = render::CameraFromOrbit(seed);
        view.verticalFov         = 40;
        view.nearPlane           = .05f;
        view.farPlane            = 150;
        window.SetViewCamera(view);
        window.EnableViewOrbitControls(seed.distance);
        auto drag = [&](int dx) {
            press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            push(press);
            move.motion.xrel = float(dx);
            move.motion.yrel = 0;
            push(move);
            press.type = SDL_EVENT_MOUSE_BUTTON_UP;
            push(press);
            require(window.PollEvents(), "Window closed during full-camera orbit");
        };
        drag(2);
        const auto draggedView = window.GetViewCamera();
        require(draggedView.position != view.position, "Full camera did not move on drag");
        for (size_t i = 0; i < view.position.size(); ++i)
            require(std::abs(draggedView.position[i] - view.position[i]) < .1f, "Initial drag snapped the camera");
        drag(-2);
        const auto returned = window.GetViewCamera();
        for (size_t i = 0; i < view.position.size(); ++i)
            require(std::abs(returned.position[i] - view.position[i]) < .0001f, "Second drag lost the orbit pose");
        push(wheel);
        require(window.PollEvents(), "Window closed during full-camera zoom");
        const auto zoomed = window.GetViewCamera();
        require(zoomed.position != returned.position && zoomed.orientation == returned.orientation,
                "Full-camera zoom did not preserve orientation");
        require(zoomed.verticalFov == view.verticalFov && zoomed.nearPlane == view.nearPlane
                && zoomed.farPlane == view.farPlane && zoomed.exposure == view.exposure,
                "Full-camera orbit changed the lens");

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
        SDL_Event close{};
        close.type            = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        close.window.windowID = id;
        push(close);
        require(!window.PollEvents(), "SDL close request not reported");
        window.CancelClose();
        require(window.PollEvents(), "CancelClose did not reopen the event loop");
        SDL_Event lifecycle{};
        lifecycle.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
        push(lifecycle);
        require(window.PollEvents(), "Backgrounding closed the window");
        require(window.DrawFrame() == render::FrameResult::Skipped, "Background frame was submitted");
        lifecycle.type = SDL_EVENT_DID_ENTER_FOREGROUND;
        push(lifecycle);
        require(window.PollEvents(), "Foregrounding closed the window");
        require(window.DrawFrame() == render::FrameResult::Rendered, "Rendering did not resume in the foreground");
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
