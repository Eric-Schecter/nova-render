#include "../common/ModelOptions.hpp"
#include "../common/PerformancePanel.hpp"
#include <iostream>
int main(int argc, char** argv)
{
    try
    {
        auto           options = example::ParseModelOptions(argc, argv, "Nova Render | Hybrid", true);
        render::Window window(options.window);
        auto           matrix = [](float x, float y, float z, float tx, float ty, float tz) {
            return render::Matrix4{x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, tx, ty, tz, 1};
        };
        window.SetScene(
          {{"box", options.modelPath}, {"floor", options.modelPath}},
          {{"ground", "floor", matrix(2, .06f, 2, 0, -.8f, 0)}, {"caster", "box", matrix(.3f, 1, .3f, 0, 0, 0)}});
        auto material      = window.GetMaterials("box").at(0);
        material.baseColor = {.8f, .8f, .8f, 1};
        material.metallic  = 0;
        material.roughness = .7f;
        window.SetMaterial(0, material, "box");
        auto floor      = material;
        floor.metallic  = .9f;
        floor.roughness = .08f;
        window.SetMaterial(0, floor, "floor");
        window.SetLighting(render::LightingMode::PBR);
        render::DirectionalLight light;
        light.direction = {-1, 2, 1};
        light.intensity = 4;
        window.SetDirectionalLight(light);
        render::OrbitCamera camera;
        camera.yaw      = 30;
        camera.pitch    = 25;
        camera.distance = 5.5f;
        window.SetCamera(camera);
        render::HybridSettings hybrid;
        hybrid.ambientOcclusion = true;
        hybrid.reflections       = true;
        hybrid.reflectionSamples = 4;
        window.SetHybrid(hybrid);
        window.SetRenderMode(render::RenderMode::Hybrid);
        window.EnableOrbitControls();
        example::PerformancePanel    performance;
        render::ParameterPanelConfig panel;
        performance.Attach(panel);
        window.EnableParameterPanel(panel);
        int frames = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
            if (performance.DrawFrame(window, window.GetFramebufferSize()) == render::FrameResult::Rendered)
                ++frames;
        window.WaitIdle();
        std::cout << "Rendered " << frames << " Hybrid frames; validation errors: " << window.ValidationErrorCount()
                  << '\n';
        return window.ValidationErrorCount() ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
