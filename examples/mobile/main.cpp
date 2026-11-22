#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <exception>
#include <filesystem>
#include <memory>
#include <render/Window.hpp>
#include <stdexcept>

#ifdef NOVA_MOBILE_HELMET
namespace
{
    std::filesystem::path HelmetPath()
    {
        constexpr auto name = "DamagedHelmet.glb";
#if defined(SDL_PLATFORM_ANDROID)
        const auto* directory = SDL_GetAndroidInternalStoragePath();
        if (!directory)
            throw std::runtime_error(SDL_GetError());
        const auto path = std::filesystem::path(directory) / name;
        size_t size = 0;
        const std::unique_ptr<void, decltype(&SDL_free)> asset(SDL_LoadFile(name, &size), SDL_free);
        if (!asset || !SDL_SaveFile(path.c_str(), asset.get(), size))
            throw std::runtime_error(SDL_GetError());
        return path;
#else
        const auto* directory = SDL_GetBasePath();
        if (!directory)
            throw std::runtime_error(SDL_GetError());
        return std::filesystem::path(directory) / name;
#endif
    }
} // namespace
#endif

int main(int, char**)
{
    try
    {
        render::WindowConfig config;
#ifdef NOVA_MOBILE_HELMET
        config.title = "Nova Damaged Helmet";
#else
        config.title = "Nova Triangle";
#endif
        render::Window window(config);
#ifdef NOVA_MOBILE_HELMET
        window.LoadModel(HelmetPath());
        render::OrbitCamera camera;
        camera.yaw      = 20;
        camera.pitch    = 5;
        camera.distance = 7.5f;
        window.SetCamera(camera);
        window.EnableOrbitControls();
#endif
        std::array<uint32_t, 2> lastSize{};
        while (window.PollEvents())
        {
            if (window.DrawFrame() != render::FrameResult::Rendered)
                continue;
            const auto size = window.GetFramebufferSize();
            if (size != lastSize)
            {
                SDL_Log("%s presented at %ux%u", config.title.c_str(), size[0], size[1]);
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
