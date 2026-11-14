#include <iostream>
#include <render/Window.hpp>
#include <stdexcept>
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            return 1;
        render::WindowConfig config;
        const std::string    api = argv[1];
        if (api == "metal")
            config.renderer.api = render::GraphicsAPI::Metal;
        else if (api == "vulkan")
            config.renderer.api = render::GraphicsAPI::Vulkan;
        else
        {
            config.renderer.api     = render::GraphicsAPI::D3D12;
            config.renderer.useWarp = true;
        }
        render::Window              window(config);
        const std::filesystem::path root = argv[2];
        window.LoadModel(root / "SkinningRobot/SkinningRobot.glb");
        window.SetLighting(render::LightingMode::PBR);
        window.EnableParameterPanel();
        if (window.GetAnimations().size() != 2 || window.GetCapabilities().pathTracing)
            throw std::runtime_error("Animation/capability API mismatch");
        auto draw = [&] {
            window.PollEvents();
            if (window.DrawFrame() != render::FrameResult::Rendered)
                throw std::runtime_error("Animation frame skipped");
        };
        draw();
        auto state    = window.GetAnimationState();
        state.playing = false;
        state.time    = .5f;
        window.SetAnimationState(state);
        draw();
        window.UpdateAnimation(.25f);
        if (window.GetAnimationState().time != .5f)
            throw std::runtime_error("Pause failed");
        state.clip    = 1;
        state.playing = true;
        state.speed   = 2;
        state.time    = 0;
        window.SetAnimationState(state);
        window.UpdateAnimation(.25f);
        draw();
        if (window.GetAnimationState().time != .5f)
            throw std::runtime_error("Speed/clip failed");
        window.LoadEnvironment(root / "Environment/venice_sunset.nenv");
        draw();
        if (window.GetAnimationState().clip != 1 || window.GetAnimationState().time != .5f)
            throw std::runtime_error("Environment reset animation");
        bool rejected = false;
        try
        {
            window.SetRenderMode(render::RenderMode::PathTracing);
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        if (!rejected || window.GetRenderMode() != render::RenderMode::Raster)
            throw std::runtime_error("Animated path mode accepted");
        state         = window.GetAnimationState();
        auto invalid  = state;
        invalid.speed = 0;
        rejected      = false;
        try
        {
            window.SetAnimationState(invalid);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected || window.GetAnimationState().speed != 2)
            throw std::runtime_error("Invalid state changed animation");
        window.LoadModel(root / "DamagedHelmet/DamagedHelmet.glb");
        draw();
        if (!window.GetAnimations().empty())
            throw std::runtime_error("Old animation retained");
        window.LoadModel(root / "SkinningRobot/SkinningRobot.glb");
        draw();
        window.EnableParameterPanel(false);
        window.UpdateAnimation(.25);
        draw();
        window.WaitIdle();
        if (window.ValidationErrorCount())
            throw std::runtime_error("Animation GPU validation errors");
        std::cout << "PASS animation GPU frames, clips, pause/seek/speed, environment preservation, capability "
                     "rejection, static switch and UI lifecycle\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
