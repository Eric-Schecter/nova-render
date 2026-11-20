#include "../common/ProceduralOptions.hpp"
#include "Lighting.hpp"
#include <chrono>
#include <iostream>
#ifdef NOVA_EXAMPLE_UI
#include "ProbeOverlay.hpp"
#endif
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
        auto floorMaterial = window.GetMaterials("floor").at(0);
        floorMaterial.roughness = .35f;
        floorMaterial.hybridReflectionStrength = 1.f;
        window.SetMaterial(0, floorMaterial, "floor");
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
        settings.probeGI                   = window.GetHybridCapabilities().probeGI;
        settings.diffuseBounce             = !settings.probeGI;
        settings.probeBounds               = HybridProbeBounds{{-2.65f, -.12f, -3.25f}, {2.65f, 1.95f, 3.25f}};
        settings.reflections               = true;
        settings.reflectionStrength        = .25f;
        settings.reflectionSamples         = 1;
        settings.halfResolutionReflections = true;
        settings.reflectionDenoising       = false;
        settings.shadows                   = true;
        settings.shadowSamples             = 4;
        window.SetHybrid(settings);
        auto lighting = example::ShowroomLighting::QuarryDaylight;
        auto applyLighting = [&] {
            const bool outdoors = lighting == example::ShowroomLighting::QuarryDaylight;
            window.LoadEnvironment(assets / (outdoors ? "QuarryClouds.nenv" : "CleanStudio.nenv"));
            EnvironmentSettings environment;
            environment.rotation       = -90;
            environment.intensity      = outdoors ? .6f : 1.f;
            environment.showBackground = false;
            window.SetEnvironment(environment);
            post.exposureEV = outdoors ? 1.39f : 0.f;
            window.SetPostProcessing(post);
            window.SetLights(example::ShowroomLights(lighting));
        };
        applyLighting();
        auto appliedLighting = lighting;
        AntiAliasingSettings aa;
        aa.method = AAMethod::FXAA;
        window.SetAntiAliasing(aa);
        window.EnableViewOrbitControls(5.784566f);
#ifdef NOVA_EXAMPLE_UI
        bool showProbes = false;
        {
            ParameterPanelConfig panel;
            panel.showBuiltinPanel = false;
            panel.drawUI           = [&] {
                example::DrawProbeOverlay(window, *settings.probeBounds, settings.probeGI, showProbes, lighting);
            };
            window.EnableParameterPanel(panel);
        }
#endif
        using Clock                      = std::chrono::steady_clock;
        constexpr auto    settleDelay    = std::chrono::milliseconds(180);
        auto              previousCamera = window.GetViewCamera();
        Clock::time_point settleAt{};
        int               frames = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            if (appliedLighting != lighting)
            {
                applyLighting();
                appliedLighting = lighting;
            }
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
