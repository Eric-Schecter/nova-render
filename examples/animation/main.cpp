#include "../common/AssetChoices.hpp"
#include "../common/ModelOptions.hpp"
#include "../common/PerformancePanel.hpp"
#include <chrono>
#include <iostream>
int main(int argc, char** argv)
{
    try
    {
        const auto options =
          example::ParseModelOptions(argc, argv, "Nova Render | Skeletal Animation", true, false, true, 3);
        render::Window window(options.window);
        window.LoadModel(options.modelPath);
        window.SetRenderMode(options.mode);
        window.SetLighting(render::LightingMode::PBR);
        window.SetDirectionalLight(options.light);
        render::SceneShadowSettings shadows;
        shadows.enabled = true;
        window.SetShadows(shadows);
        window.LoadEnvironment(options.environmentPath);
        window.SetEnvironment(render::EnvironmentSettings{});
        render::OrbitCamera camera;
        camera.yaw      = 65;
        camera.pitch    = 10;
        camera.distance = 3;
        window.SetCamera(camera);
        window.EnableOrbitControls();
        example::PerformancePanel    performance;
        render::ParameterPanelConfig panel;
        panel.environments = example::EnvironmentChoices(options.environmentPath);
        performance.Attach(panel);
        window.EnableParameterPanel(panel);
        if (window.GetAnimations().empty())
            throw std::runtime_error("Load a GLB with skeletal animation clips");
        auto previous = std::chrono::steady_clock::now();
        int  frames   = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            const auto now = std::chrono::steady_clock::now();
            window.UpdateAnimation(std::chrono::duration<float>(now - previous).count());
            previous = now;
            if (performance.DrawFrame(window, window.GetFramebufferSize()) == render::FrameResult::Rendered)
                ++frames;
        }
        window.WaitIdle();
        std::cout << "Rendered " << frames << " animation frames; time: " << window.GetAnimationState().time
                  << "; validation errors: " << window.ValidationErrorCount() << '\n';
        return window.ValidationErrorCount() || (options.frameLimit && frames != options.frameLimit) ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
