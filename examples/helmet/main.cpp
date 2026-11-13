#include "../common/AssetChoices.hpp"
#include "../common/ModelOptions.hpp"
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        const auto     options = example::ParseModelOptions(argc, argv, "Nova | Model Loading");
        render::Window window(options.window);
        window.LoadModel(options.modelPath);
        render::OrbitCamera camera;
        camera.yaw   = 20;
        camera.pitch = 5;
        window.SetCamera(camera);
        window.EnableOrbitControls();
        render::ParameterPanelConfig panel;
        panel.models = example::ModelChoices(options.modelPath);
        window.EnableParameterPanel(panel);
        int frames = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            if (options.spin)
            {
                camera.yaw = 20.0f + frames * 0.25f;
                window.SetCamera(camera);
            }
            if (window.DrawFrame() == render::FrameResult::Rendered)
                ++frames;
        }
        window.WaitIdle();
        std::cout << "Rendered " << frames << " frames; validation errors: " << window.ValidationErrorCount() << '\n';
        return window.ValidationErrorCount() == 0 && (!options.frameLimit || frames == options.frameLimit) ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
