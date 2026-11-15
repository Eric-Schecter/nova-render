#include "../common/AssetChoices.hpp"
#include "../common/ModelOptions.hpp"
#include "../common/PerformancePanel.hpp"
#include "ComparisonScene.hpp"
#include <iostream>
#ifdef NOVA_EXAMPLE_UI
#include <imgui.h>
#endif
int main(int argc, char** argv)
{
    try
    {
        auto options          = example::ParseModelOptions(argc, argv, "Nova | Integrator comparison scene");
        options.window.width  = 960;
        options.window.height = 1200;
        render::Window           window(options.window);
        example::ComparisonScene scene(options.window.renderer);
        if (!scene.Supported())
            return 77;
        scene.LoadModel(options.modelPath);
        auto settings         = options.path;
        settings.maxBounces   = 8;
        settings.photonRadius = .1f;
        scene.SetSettings(settings);
        example::ComparisonBudget budget{options.comparisonSamples, options.comparisonMilliseconds};
        scene.SetBudget(budget);
        render::OrbitCamera camera;
        camera.exposure = 3;
        window.SetCamera(camera);
        scene.SetCamera(camera);
        window.EnableOrbitControls();
        const auto                choices   = example::ModelChoices(options.modelPath);
        std::filesystem::path     modelPath = options.modelPath, requested;
        bool                      reset = false, settingsChanged = false, budgetChanged = false, cameraChanged = false;
        example::PerformancePanel performance;
        performance.method = "Comparison (4 views)";
#ifdef NOVA_EXAMPLE_UI
        render::ParameterPanelConfig panel;
        panel.showBuiltinPanel = false;
        panel.drawUI           = [&] {
            const auto  display = ImGui::GetIO().DisplaySize;
            const char* names[] = {"PT", "BDPT", "Photon mapping", "MLT"};
            for (size_t i = 0; i < 4; ++i)
            {
                const auto& p = scene.Progress()[i];
                char        label[160];
                std::snprintf(
                  label,
                  sizeof(label),
                  "%s | %u %s | %.0f ms%s",
                  names[i],
                  p.samples,
                  i == 3 ? "mpp" : "spp",
                  p.milliseconds,
                  p.complete ? " | paused" : "");
                ImGui::GetBackgroundDrawList()->AddText(
                  {(display.x - std::min(display.x, display.y * .8f)) / 2
                     + float(i % 2) * std::min(display.x, display.y * .8f) / 2 + 5,
                   40
                     + ((display.y * .8f - std::min(display.x, display.y * .8f)) / 2
                        + float(i / 2) * std::min(display.x, display.y * .8f) / 2 + 5)
                         * (display.y - 40) / display.y},
                  IM_COL32_WHITE,
                  label);
            }
            ImGui::SetNextWindowPos({0, display.y * .8f});
            ImGui::SetNextWindowSize({display.x, display.y * .2f});
            ImGui::Begin(
              "Comparison scene",
              nullptr,
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
            if (ImGui::BeginCombo("Scene", example::AssetName(modelPath).c_str()))
            {
                for (const auto& c : choices)
                    if (ImGui::Selectable(c.name.c_str(), c.path == modelPath))
                        requested = c.path;
                ImGui::EndCombo();
            }
            int n = int(budget.samples), ms = int(budget.milliseconds);
            if (ImGui::SliderInt("Samples / method", &n, 1, 65536))
            {
                budget.samples = uint32_t(n);
                budgetChanged  = true;
            }
            if (ImGui::SliderInt("Time / method (ms, 0 = samples)", &ms, 0, 60000))
            {
                budget.milliseconds = uint32_t(ms);
                budgetChanged       = true;
            }
            int b = int(settings.maxBounces);
            if (ImGui::SliderInt("Max bounces", &b, 1, 8))
            {
                settings.maxBounces = uint32_t(b);
                settingsChanged     = true;
            }
            if (ImGui::SliderFloat("Photon radius", &settings.photonRadius, .03f, .5f))
                settingsChanged = true;
            int photons = int(settings.photonsPerIteration), bootstrap = int(settings.mltBootstrapSamples);
            if (ImGui::SliderInt("Photons / iteration", &photons, 256, 65536))
            {
                settings.photonsPerIteration = uint32_t(photons);
                settingsChanged              = true;
            }
            if (ImGui::SliderFloat("Render scale", &settings.resolutionScale, .25f, 1))
                settingsChanged = true;
            if (ImGui::SliderInt("MLT bootstrap", &bootstrap, 256, 1048576))
            {
                settings.mltBootstrapSamples = uint32_t(bootstrap);
                settingsChanged              = true;
            }
            if (ImGui::SliderFloat("MLT large step", &settings.mltLargeStepProbability, .05f, 1))
                settingsChanged = true;
            if (ImGui::SliderFloat("MLT mutation scale", &settings.mltMutationScale, .0001f, .25f, "%.4f"))
                settingsChanged = true;
            if (ImGui::SliderFloat("Yaw", &camera.yaw, -180, 180))
                cameraChanged = true;
            if (ImGui::SliderFloat("Pitch", &camera.pitch, -85, 85))
                cameraChanged = true;
            if (ImGui::SliderFloat("Distance", &camera.distance, 1.5f, 100))
                cameraChanged = true;
            if (ImGui::SliderFloat("Exposure", &camera.exposure, .1f, 3))
                cameraChanged = true;
            if (ImGui::Button("Reset accumulation"))
                reset = true;
            ImGui::SameLine();
            ImGui::TextUnformatted("Emitter-only | no denoising | drag scene to orbit");
            ImGui::End();
        };
        performance.Attach(panel);
        window.EnableParameterPanel(panel);
#endif
        int frames = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            if (!requested.empty())
            {
                scene.LoadModel(requested);
                modelPath = requested;
                requested.clear();
            }
            if (settingsChanged)
            {
                scene.SetSettings(settings);
                settingsChanged = false;
            }
            if (budgetChanged)
            {
                scene.SetBudget(budget);
                budgetChanged = false;
            }
            if (reset)
            {
                scene.Reset();
                reset = false;
            }
            if (cameraChanged)
            {
                window.SetCamera(camera);
                cameraChanged = false;
            }
            else
                camera = window.GetCamera();
            scene.SetCamera(camera);
            const auto framebuffer = window.GetFramebufferSize();
            if (!framebuffer[0] || !framebuffer[1])
            {
                performance.active = false;
                performance.meter.FPS(false);
                continue;
            }
            const float outputScale = std::min(1.f, 1024.f / float(std::max(framebuffer[0], framebuffer[1])));
            const auto  width       = std::max(2u, uint32_t(float(framebuffer[0]) * outputScale));
            const auto  height      = std::max(2u, uint32_t(float(framebuffer[1]) * outputScale));
            const auto  tile        = std::max(1u, std::min(width / 2, uint32_t(float(height) * .4f)));
            const bool  updating    = performance.width != std::max(1u, uint32_t(tile * settings.resolutionScale)) * 2
                                  || std::any_of(scene.Progress().begin(), scene.Progress().end(), [](const auto& p) {
                                         return !p.complete;
                                     });
            const auto    start = example::FrameMeter::Clock::now();
            const auto&   grid  = scene.DrawFrame(tile);
            render::Image image;
            image.width  = std::max(grid.width, uint32_t(float(width) * settings.resolutionScale));
            image.height = std::max(grid.height, uint32_t(float(height) * settings.resolutionScale));
            image.pixels.assign(size_t(image.width) * image.height * 4, 0);
            const auto left = (image.width - grid.width) / 2;
            const auto top  = (std::max(grid.height, uint32_t(float(image.height) * .8f)) - grid.height) / 2;
            for (uint32_t y = 0; y < grid.height; ++y)
                std::memcpy(
                  image.pixels.data() + ((y + top) * image.width + left) * 4,
                  grid.pixels.data() + size_t(y) * grid.width * 4,
                  size_t(grid.width) * 4);
            window.PresentImage(image);
            if (window.DrawFrame() == render::FrameResult::Rendered)
            {
                window.WaitIdle();
                if (updating)
                    performance.meter.Record(start);
                performance.active = std::any_of(
                  scene.Progress().begin(), scene.Progress().end(), [](const auto& p) { return !p.complete; });
                performance.width       = grid.width;
                performance.height      = grid.height;
                performance.scale       = settings.resolutionScale;
                performance.samples     = 1;
                performance.accumulated = scene.Progress()[0].samples;
                ++frames;
            }
        }
        window.WaitIdle();
        for (const auto& p : scene.Progress())
            std::cout << int(p.integrator) << ": " << p.samples << " samples/mutations, " << p.milliseconds
                      << " ms, paused=" << p.complete << '\n';
        return window.ValidationErrorCount() ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
