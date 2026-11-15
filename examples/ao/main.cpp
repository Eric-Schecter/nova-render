#include "../common/ModelOptions.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <render/RayTracingView.hpp>
#ifdef NOVA_EXAMPLE_UI
#include <imgui.h>
#endif
int main(int argc, char** argv)
{
    try
    {
        auto options          = example::ParseModelOptions(argc, argv, "Nova | Ambient occlusion");
        options.window.width  = 1100;
        options.window.height = 900;
        render::Window         window(options.window);
        render::RayTracingView view(options.window.renderer);
        if (!view.GetCapabilities().rayTracing)
            return 77;
        view.LoadModel(options.modelPath);
        render::AmbientOcclusionSettings ao;
        ao.enabled  = true;
        ao.method   = render::AOMethod::HBAO;
        ao.strength = 1.5f;
        view.SetAmbientOcclusion(ao);
        render::OrbitCamera camera;
        camera.yaw      = 90;
        camera.pitch    = 3;
        camera.distance = 1.5f;
        camera.target   = {-1.35f, -.47850394f, -.25f};
        window.SetCamera(camera);
        window.EnableOrbitControls();
        bool changed = true, cameraChanged = false;
        int  preset = 0;
#ifdef NOVA_EXAMPLE_UI
        render::ParameterPanelConfig panel;
        panel.showBuiltinPanel = false;
        panel.drawUI           = [&] {
            const auto display = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowPos({0, display.y * .8f});
            ImGui::SetNextWindowSize({display.x, display.y * .2f});
            ImGui::Begin(
              "Ambient occlusion",
              nullptr,
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
            int method = int(ao.method), output = int(ao.output), samples = int(ao.samples);
            if (ImGui::Combo("Method", &method, "Off\0SSAO\0HBAO\0MSAO\0RTAO\0"))
            {
                ao.method = static_cast<render::AOMethod>(method);
                changed   = true;
            }
            if (ImGui::Combo("Display", &output, "Composite\0AO only\0Depth\0Normals\0"))
            {
                ao.output = static_cast<render::AOOutput>(output);
                changed   = true;
            }
            if (ImGui::SliderFloat("Radius", &ao.radius, .001f, 1, "%.3f"))
                changed = true;
            if (ImGui::SliderFloat("Bias", &ao.bias, 0, .05f, "%.4f"))
                changed = true;
            if (ImGui::SliderFloat("Strength", &ao.strength, 0, 4))
                changed = true;
            if (ImGui::SliderInt("Samples", &samples, 4, 64))
            {
                ao.samples = uint32_t(samples);
                changed    = true;
            }
            if (ImGui::SliderFloat("Render scale", &ao.resolutionScale, .25f, 1))
                changed = true;
            if (ImGui::Checkbox("Edge-aware filter", &ao.denoise))
                changed = true;
            if (ImGui::Combo("View", &preset, "Column arcade\0Doorway\0Overview\0"))
            {
                camera = {};
                if (preset == 0)
                {
                    camera.yaw      = 90;
                    camera.pitch    = 3;
                    camera.distance = 1.5f;
                    camera.target   = {-1.35f, -.47850394f, -.25f};
                }
                else if (preset == 1)
                {
                    camera.yaw      = 90;
                    camera.pitch    = 3;
                    camera.distance = 1.5f;
                    camera.target   = {-1.35f, -.47850394f, .12f};
                }
                else
                {
                    camera.yaw      = 35;
                    camera.pitch    = 20;
                    camera.distance = 2.8f;
                }
                cameraChanged = true;
            }
            ImGui::Text(
              "Frames: %u%s", view.GetAccumulatedSamples(), ao.method == render::AOMethod::RTAO ? " / 32" : "");
            ImGui::End();
        };
        window.EnableParameterPanel(panel);
#endif
        render::Image           output;
        std::array<uint32_t, 2> previousSize{};
        render::OrbitCamera     previousCamera = camera;
        int                     frames         = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            if (cameraChanged)
            {
                window.SetCamera(camera);
                cameraChanged = false;
            }
            else
                camera = window.GetCamera();
            const auto framebuffer = window.GetFramebufferSize();
            if (!framebuffer[0] || !framebuffer[1])
                continue;
            const float    scale  = std::min(1.f, 1024.f / float(std::max(framebuffer[0], framebuffer[1])));
            const uint32_t width  = std::max(2u, uint32_t(framebuffer[0] * scale)),
                           height = std::max(2u, uint32_t(framebuffer[1] * scale * .8f));
            if (
              std::memcmp(&camera, &previousCamera, sizeof(camera))
              || previousSize != std::array<uint32_t, 2>{width, height})
                changed = true;
            if (changed)
            {
                view.SetCamera(camera);
                view.SetAmbientOcclusion(ao);
                previousCamera = camera;
                previousSize   = {width, height};
                changed        = false;
                output         = {};
            }
            if (output.pixels.empty() || (ao.method == render::AOMethod::RTAO && view.GetAccumulatedSamples() < 32))
            {
                output        = view.DrawFrame(width, height);
                output.height = output.height * 5 / 4;
                output.pixels.resize(size_t(output.width) * output.height * 4, 0);
            }
            window.PresentImage(output);
            if (window.DrawFrame() == render::FrameResult::Rendered)
                ++frames;
        }
        window.WaitIdle();
        std::cout << "AO frames: " << view.GetAccumulatedSamples()
                  << "; validation errors: " << window.ValidationErrorCount() << '\n';
        return window.ValidationErrorCount() ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
