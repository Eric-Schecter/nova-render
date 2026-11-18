#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <exception>
#include <render/Window.hpp>

int main(int, char**)
{
    try
    {
        render::WindowConfig config;
        config.title        = "Nova Triangle";
        config.renderer.api = render::GraphicsAPI::Metal;
        render::Window          window(config);
        std::array<uint32_t, 2> lastSize{};
        while (window.PollEvents())
        {
            if (window.DrawFrame() != render::FrameResult::Rendered)
                continue;
            const auto size = window.GetFramebufferSize();
            if (size != lastSize)
            {
                SDL_Log("Nova triangle presented at %ux%u", size[0], size[1]);
                lastSize = size;
            }
        }
        window.WaitIdle();
        return window.ValidationErrorCount() ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
        return 1;
    }
}
