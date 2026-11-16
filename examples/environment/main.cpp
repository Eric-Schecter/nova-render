#include "../common/AssetChoices.hpp"
#include "../common/ModelOptions.hpp"
#include "../common/PerformancePanel.hpp"
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        const auto     options = example::ParseModelOptions(argc, argv, "Nova Render | Skybox and Environment", true, true);
        render::Window window(options.window);
        window.LoadModel(options.modelPath);
        window.SetLighting(render::LightingMode::PBR);
        window.SetDirectionalLight(options.light);
        window.LoadEnvironment(options.environmentPath);
        window.SetEnvironment(render::EnvironmentSettings{});
        render::OrbitCamera camera;
        camera.yaw   = 20;
        camera.pitch = 5;
        window.SetCamera(camera);
        window.SetRayTracing(options.path);
        window.SetRenderMode(options.mode);
        window.EnableOrbitControls();
        example::PerformancePanel    performance;
        render::ParameterPanelConfig panel;
        panel.models       = example::ModelChoices(options.modelPath);
        panel.environments = example::EnvironmentChoices(options.environmentPath);
        performance.Attach(panel);
        window.EnableParameterPanel(panel);
        int frames = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            if (options.spin)
            {
                camera.yaw = 20.0f + frames * 0.25f;
                window.SetCamera(camera);
            }
            if (performance.DrawFrame(window, window.GetFramebufferSize()) == render::FrameResult::Rendered)
                ++frames;
        }
        window.WaitIdle();
        if (window.GetRenderMode() == render::RenderMode::PathTracing)
            std::cout << "Path samples: " << window.GetAccumulatedSamples() << '\n';
        std::cout << "Rendered " << frames << " frames; validation errors: " << window.ValidationErrorCount() << '\n';
        return window.ValidationErrorCount() == 0 && (!options.frameLimit || frames == options.frameLimit) ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
