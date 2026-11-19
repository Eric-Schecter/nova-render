#include "../common/ProceduralOptions.hpp"
#include <chrono>
#include <iostream>
using namespace render;
int main(int argc, char** argv)
{
    try
    {
        auto options          = example::ParseProceduralOptions(argc, argv, "CarConcept Showroom");
        options.window.width  = 750;
        options.window.height = 473;
        Window                      window(options.window);
        const std::filesystem::path assets = "assets/CarConcept";
        PostProcessingSettings      post;
        post.enabled      = true;
        post.bloom        = false;
        post.autoExposure = false;
        post.toneMapping  = ToneMapping::ACES;
        window.SetPostProcessing(post);
        window.SetClearColor({.2f, .2f, .2f, 1});
        window.SetLighting(LightingMode::PBR);
        SceneInstance car{"car", "car"};
        car.world[12] = .005216949f;
        // Seat the tire tread on the floor while keeping the body clear.
        car.world[13] = -.00122019f;
        car.world[14] = -.23846179f;
        window.SetScene(
          {{"car", assets / "CarConcept.glb", true}, {"floor", assets / "Ground.glb", true}},
          {car, {"floor", "floor"}});
        window.LoadEnvironment(assets / "CleanStudio.nenv");
        EnvironmentSettings environment;
        environment.showBackground = false;
        window.SetEnvironment(environment);
        Camera camera;
        camera.position    = {4.0087951f, 1.6122328f, 4.052369f};
        camera.orientation = {-.078989f, .37879612f, .03246848f, .92153135f};
        camera.verticalFov = 40;
        camera.nearPlane   = .05f;
        camera.farPlane    = 150;
        camera.exposure    = .85f;
        window.SetViewCamera(camera);
        window.SetRenderMode(RenderMode::Hybrid);
        HybridSettings settings;
        settings.reflections               = true;
        settings.reflectionSamples         = 1;
        settings.halfResolutionReflections = true;
        settings.reflectionDenoising       = false;
        settings.shadows                   = true;
        settings.shadowSamples             = 4;
        window.SetHybrid(settings);
        SceneLight hood;
        hood.id         = "hood";
        hood.type       = LightType::Rectangle;
        hood.position   = {-.4f, 3.25f, 1.15f};
        hood.direction  = {0, -1, 0};
        hood.up         = {0, 0, 1};
        hood.width      = 4.7f;
        hood.height     = 1.4f;
        hood.intensity  = 2.5f;
        hood.color      = {1, .94f, .86f};
        SceneLight side = hood;
        side.id         = "side";
        side.position   = {3.15f, 1.25f, -.35f};
        side.direction  = {-1, -.15f, 0};
        side.up         = {0, 1, 0};
        side.width      = 5.1f;
        side.height     = .55f;
        side.intensity  = 3.2f;
        side.color      = {.88f, .95f, 1};
        hood.castShadow = true;
        side.castShadow = false;
        SceneLight fill = side;
        fill.id         = "fill";
        fill.position   = {2.9f, .85f, -.65f};
        fill.direction  = {-1, .14f, -.08f};
        fill.width      = 5.8f;
        fill.height     = 1.35f;
        fill.intensity  = .55f;
        fill.color      = {1, 1, 1};
        window.SetLights({hood, side, fill});
        AntiAliasingSettings aa;
        aa.method = AAMethod::FXAA;
        window.SetAntiAliasing(aa);
        window.EnableViewOrbitControls(5.784566f);
        using Clock                      = std::chrono::steady_clock;
        constexpr auto    settleDelay    = std::chrono::milliseconds(180);
        auto              previousCamera = window.GetViewCamera();
        Clock::time_point settleAt{};
        int               frames = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            const auto now           = Clock::now();
            const auto currentCamera = window.GetViewCamera();
            if (
              currentCamera.position != previousCamera.position
              || currentCamera.orientation != previousCamera.orientation)
            {
                settleAt       = now + settleDelay;
                previousCamera = currentCamera;
            }
            const unsigned shadowSamples = now < settleAt ? 2 : 4;
            if (settings.shadowSamples != shadowSamples)
            {
                settings.shadowSamples = shadowSamples;
                window.SetHybrid(settings);
            }
            if (window.DrawFrame() == FrameResult::Rendered)
                ++frames;
        }
        window.WaitIdle();
        const auto errors = window.ValidationErrorCount();
        std::cout << "Rendered " << frames << " showroom frames; validation errors: " << errors << '\n';
        return errors ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
