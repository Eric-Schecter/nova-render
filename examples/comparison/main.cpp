#include "../common/AssetChoices.hpp"
#include "../common/ModelOptions.hpp"
#include <iostream>
int main(int argc, char** argv)
{
    try
    {
        auto options          = example::ParseModelOptions(argc, argv, "Nova | Integrator comparison");
        options.window.width  = 1600;
        options.window.height = 600;
        render::Window window(options.window);
        window.LoadModel(options.modelPath);
        render::OrbitCamera camera;
        camera.exposure = 3;
        window.SetCamera(camera);
        auto settings         = options.path;
        settings.maxBounces   = 8;
        settings.photonRadius = .1f;
        window.SetRayTracing(settings);
        window.SetRenderMode(render::RenderMode::IntegratorComparison);
        window.EnableOrbitControls();
        render::ParameterPanelConfig panel;
        panel.models = example::ModelChoices(options.modelPath);
        window.EnableParameterPanel(panel);
        int frames = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            if (window.DrawFrame() == render::FrameResult::Rendered)
                ++frames;
        }
        window.WaitIdle();
        for (const auto& progress : window.GetComparisonProgress())
            std::cout << int(progress.integrator) << ": " << progress.samples
                      << (progress.integrator == render::RayIntegrator::Metropolis ? " mpp, " : " spp, ")
                      << progress.milliseconds << " ms, paused=" << progress.complete << '\n';
        return window.ValidationErrorCount() ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
