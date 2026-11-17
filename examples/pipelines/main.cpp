#include "../common/FloatingPanel.hpp"
#include "../common/PerformancePanel.hpp"
#include "../common/ProceduralOptions.hpp"
#include <iostream>
#include <render/PipelineComparisonView.hpp>
#ifdef NOVA_EXAMPLE_UI
#include <imgui.h>
#endif
int main(int argc, char** argv)
{
    try
    {
        auto options          = example::ParseProceduralOptions(argc, argv, "Nova Render | Raster pipelines");
        options.window.width  = 1100;
        options.window.height = 800;
        render::Window                 window(options.window);
        render::PipelineComparisonView view(options.window.renderer);
        auto                           settings = view.GetSettings();
        window.SetCamera(view.GetCamera());
        window.EnableOrbitControls();
        const char*                  names[] = {"Forward", "Deferred", "Forward+", "Clustered Forward"};
        example::PerformancePanel    performance;
        render::ParameterPanelConfig panel;
        panel.showBuiltinPanel = false;
        bool continuous = false, changed = true;
#ifdef NOVA_EXAMPLE_UI
        panel.drawUI = [&] {
            example::PlaceFloatingPanel();
            ImGui::Begin("Raster pipelines", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            int pipeline = int(settings.pipeline), debug = int(settings.debug), lights = int(settings.lightCount);
            if (ImGui::Combo("Pipeline", &pipeline, names, 4))
            {
                settings.pipeline = render::RasterPipeline(pipeline);
                changed           = true;
            }
            if (ImGui::Combo(
                  "Output",
                  &debug,
                  "Final lighting\0Base color\0World normal\0Linear depth\0Roughness\0Metallic\0Light count\0List "
                  "overflow\0"))
            {
                settings.debug = render::PipelineDebug(debug);
                changed        = true;
            }
            if (ImGui::SliderInt("Lights", &lights, 0, 256))
            {
                settings.lightCount = uint32_t(lights);
                changed             = true;
            }
            changed |= ImGui::SliderFloat("Light radius", &settings.lightRadius, .25f, 12);
            changed |= ImGui::SliderFloat("Intensity", &settings.lightIntensity, 0, 100);
            ImGui::Checkbox("Continuous benchmark", &continuous);
            ImGui::TextUnformatted("16 x 9 tiles; 16 logarithmic depth slices");
            ImGui::TextWrapped("Over 64 lights per cell: evaluate all lights. Magenta marks overflow.");
            ImGui::End();
        };
#endif
        performance.Attach(panel);
        window.EnableParameterPanel(panel);
        int      frames = 0;
        uint32_t oldW = 0, oldH = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            auto start      = example::FrameMeter::Clock::now();
            auto camera     = window.GetCamera();
            camera.distance = std::clamp(camera.distance, 4.f, 50.f);
            camera.pitch    = std::clamp(camera.pitch, -80.f, 80.f);
            window.SetCamera(camera);
            auto old = view.GetCamera();
            if (camera.yaw != old.yaw || camera.pitch != old.pitch || camera.distance != old.distance)
            {
                view.SetCamera(camera);
                changed = true;
            }
            auto fb = window.GetFramebufferSize();
            if (!fb[0] || !fb[1])
                continue;
            float inset = 40;
#ifdef NOVA_EXAMPLE_UI
            inset *= ImGui::GetIO().DisplayFramebufferScale.y;
#endif
            float    scale = std::min(1.f, 1024.f / fb[0]);
            uint32_t w     = std::max(16u, uint32_t(fb[0] * scale)),
                     h     = std::max(9u, uint32_t(std::max(9.f, float(fb[1]) - inset) * scale));
            bool update    = changed || continuous || w != oldW || h != oldH;
            if (update)
            {
                view.SetSettings(settings);
                window.PresentImage(view.DrawFrame(w, h));
                oldW    = w;
                oldH    = h;
                changed = false;
            }
            if (window.DrawFrame() == render::FrameResult::Rendered)
            {
                window.WaitIdle();
                if (update)
                    performance.meter.Record(start);
                frames++;
            }
            performance.active = continuous;
            performance.width  = w;
            performance.height = h;
            performance.scale  = scale;
            performance.method = names[unsigned(settings.pipeline)];
        }
        window.WaitIdle();
        std::cout << "Rendered " << frames << " frames; validation errors: " << window.ValidationErrorCount() << '\n';
        return window.ValidationErrorCount() ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
