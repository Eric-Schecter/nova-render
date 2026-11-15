#include "../common/FloatingPanel.hpp"
#include "../common/PerformancePanel.hpp"
#include "../common/ProceduralOptions.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <render/AntiAliasingView.hpp>
#ifdef NOVA_EXAMPLE_UI
#include <imgui.h>
#endif
int main(int argc, char** argv)
{
    try
    {
        auto options          = example::ParseProceduralOptions(argc, argv, "Nova | Anti-aliasing");
        options.window.width  = 1100;
        options.window.height = 900;
        render::Window           window(options.window);
        render::AntiAliasingView view(options.window.renderer);
        auto                     settings = view.GetAntiAliasing();
        auto                     camera   = view.GetCamera();
        window.SetCamera(camera);
        window.EnableOrbitControls();
        example::PerformancePanel    performance;
        bool                         playing = true, changed = true, reset = false, cameraChanged = false;
        float                        scale = options.path.resolutionScale, speed = .35f, time = 0;
        int                          settle  = 32;
        const char*                  names[] = {"Off", "FXAA", "SMAA 1x", "MSAA 4x", "SSAA 2x2", "TAA"};
        render::ParameterPanelConfig panel;
        panel.showBuiltinPanel = false;
#ifdef NOVA_EXAMPLE_UI
        panel.drawUI = [&] {
            example::PlaceFloatingPanel();
            ImGui::Begin("Anti-aliasing", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            int method = int(settings.method);
            if (ImGui::Combo("Method", &method, names, 6))
            {
                settings.method = render::AAMethod(method);
                changed         = true;
            }
            changed |= ImGui::Checkbox("Texture filtering", &settings.textureFiltering);
            changed |= ImGui::Checkbox("Animate", &playing);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Split: Off | selected AA", &settings.split);
            changed |= ImGui::SliderFloat("Speed", &speed, .05f, 1);
            changed |= ImGui::SliderFloat("Render scale", &scale, .25f, 1);
            changed |= ImGui::SliderFloat("Zoom", &settings.zoom, 1, 8);
            ImGui::BeginDisabled(settings.method != render::AAMethod::TAA);
            changed |= ImGui::SliderFloat("TAA history weight", &settings.historyWeight, 0, .98f);
            ImGui::EndDisabled();
            cameraChanged |= ImGui::SliderFloat("Yaw", &camera.yaw, -180, 180);
            cameraChanged |= ImGui::SliderFloat("Pitch", &camera.pitch, -60, 60);
            cameraChanged |= ImGui::SliderFloat("Distance", &camera.distance, 4, 20);
            ImGui::BeginDisabled(settings.method != render::AAMethod::TAA);
            if (ImGui::Button("Reset history"))
                reset = true;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("History: %u | Drag to orbit, wheel to zoom", view.GetHistoryFrames());
            ImGui::End();
        };
#endif
        performance.Attach(panel);
        window.EnableParameterPanel(panel);
        auto          previous = example::FrameMeter::Clock::now();
        int           frames   = 0;
        render::Image composed;
        uint32_t      oldW = 0, oldH = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            const auto  start = example::FrameMeter::Clock::now();
            const float delta = std::min(.1f, std::chrono::duration<float>(start - previous).count());
            previous          = start;
            if (cameraChanged)
            {
                window.SetCamera(camera);
                cameraChanged = false;
            }
            else
                camera = window.GetCamera();
            camera.distance = std::clamp(camera.distance, 4.f, 20.f);
            camera.pitch    = std::clamp(camera.pitch, -60.f, 60.f);
            window.SetCamera(camera);
            const auto current = view.GetCamera();
            if (std::memcmp(&camera, &current, sizeof(camera)) != 0)
            {
                view.SetCamera(camera);
                changed = true;
            }
            if (changed)
            {
                view.SetAntiAliasing(settings);
                settle = 32;
            }
            if (reset)
            {
                view.ResetHistory();
                reset   = false;
                changed = true;
                settle  = 32;
            }
            const auto fb = window.GetFramebufferSize();
            if (!fb[0] || !fb[1])
            {
                performance.active = false;
                performance.meter.FPS(false);
                continue;
            }
            float inset = 0;
#ifdef NOVA_EXAMPLE_UI
            inset = 40 * ImGui::GetIO().DisplayFramebufferScale.y;
#endif
            const float viewportH = std::max(1.f, float(fb[1]) - inset);
#ifdef NOVA_EXAMPLE_UI
            const float sceneH = viewportH;
#else
            const float sceneH = viewportH;
#endif
            const float    limit = std::min(1.f, 1024.f / (std::max(float(fb[0]), sceneH) * scale));
            const uint32_t w     = std::max(1u, uint32_t(std::round(fb[0] * scale * limit))),
                           h     = std::max(1u, uint32_t(std::round(sceneH * scale * limit)));
            if (w != oldW || h != oldH)
            {
                changed = true;
                settle  = 32;
                oldW    = w;
                oldH    = h;
            }
            const bool update = changed || playing || (settings.method == render::AAMethod::TAA && settle > 0);
            changed           = false;
            if (update)
            {
                if (playing)
                    time += delta * speed;
                view.SetTime(time);
                const auto& image = view.DrawFrame(w, h);
                settle            = std::max(0, settle - 1);
                composed.width    = w;
                composed.height   = std::max(h, uint32_t(std::round(viewportH * scale * limit)));
                composed.pixels.assign(size_t(composed.width) * composed.height * 4, 0);
                std::memcpy(composed.pixels.data(), image.pixels.data(), image.pixels.size());
                window.PresentImage(composed);
            }
            if (window.DrawFrame() == render::FrameResult::Rendered)
            {
                window.WaitIdle();
                if (update)
                    performance.meter.Record(start);
                frames++;
            }
            performance.active = playing || (settings.method == render::AAMethod::TAA && settle > 0);
            const uint32_t ss  = settings.method == render::AAMethod::SSAA ? 2 : 1;
            performance.width  = w * ss;
            performance.height = h * ss;
            performance.scale  = scale * limit * ss;
            performance.samples =
              settings.method == render::AAMethod::MSAA || settings.method == render::AAMethod::SSAA ? 4 : 1;
            performance.method      = names[int(settings.method)];
            performance.accumulated = view.GetHistoryFrames();
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
