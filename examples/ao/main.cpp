#include "../common/FloatingPanel.hpp"
#include "../common/FrameMeter.hpp"
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
        auto options          = example::ParseModelOptions(argc, argv, "Nova Render | Ambient occlusion");
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
        bool                             changed = true, cameraChanged = false;
        bool                             adaptive = true, usingPreview = false;
        example::FrameMeter              meter;
        auto                             interactionUntil = example::FrameMeter::Clock::time_point{};
        double                           lastFullMs       = 0;
        render::AmbientOcclusionSettings applied          = ao;
        uint32_t                         renderWidth = 0, renderHeight = 0;
        int                              preset = 0;
#ifdef NOVA_EXAMPLE_UI
        render::ParameterPanelConfig panel;
        panel.showBuiltinPanel = false;
        panel.drawUI           = [&] {
            const auto display = ImGui::GetIO().DisplaySize;
            const bool active  = example::FrameMeter::Clock::now() < interactionUntil
                                || (ao.method == render::AOMethod::RTAO && view.GetAccumulatedSamples() < 32);
            const auto fps = meter.FPS(active);
            char       left[220], right[220], fpsText[32], p95Text[32];
            if (fps > 0)
                std::snprintf(fpsText, sizeof(fpsText), "%.1f", fps);
            else
                std::snprintf(fpsText, sizeof(fpsText), "--");
            if (meter.frames > 1)
                std::snprintf(p95Text, sizeof(p95Text), "%.1f", meter.P95());
            else
                std::snprintf(p95Text, sizeof(p95Text), "--");
            std::snprintf(
              left, sizeof(left), "AO %s FPS | Draw %.1f ms | P95 %s ms", fpsText, meter.lastMilliseconds, p95Text);
            const char* names[] = {"Off", "SSAO", "HBAO", "MSAO", "RTAO"};
            std::snprintf(
              right,
              sizeof(right),
              "%s | %ux%u | %.1f%% | %s | %u samples",
              usingPreview ? "Interaction preview"
                        : active     ? "Rendering"
                                     : "Idle (on demand)",
              renderWidth,
              renderHeight,
              applied.resolutionScale * 100,
              names[int(ao.method)],
              applied.samples);
            auto* draw = ImGui::GetBackgroundDrawList();
            draw->AddRectFilled({0, 0}, {display.x, 40}, IM_COL32(21, 27, 38, 255));
            draw->AddText({7, 3}, IM_COL32_WHITE, left);
            draw->AddText({7, 21}, IM_COL32_WHITE, right);
            example::PlaceFloatingPanel();
            ImGui::Begin("Ambient occlusion", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
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
            if (ImGui::Checkbox("Adaptive interaction quality", &adaptive))
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
            const float scale        = std::min(1.f, 1024.f / float(std::max(framebuffer[0], framebuffer[1])));
            float       headerPixels = 0;
#ifdef NOVA_EXAMPLE_UI
            const auto logical = ImGui::GetIO().DisplaySize;
            headerPixels       = std::min(
              float(framebuffer[1]) * .3f,
              40.f * float(framebuffer[0]) / std::max(1.f, logical.x > 0 ? logical.x : float(options.window.width)));
#endif
            const uint32_t width  = std::max(2u, uint32_t(framebuffer[0] * scale)),
                           height = std::max(2u, uint32_t((float(framebuffer[1]) - headerPixels) * scale));
            const bool motion     = std::memcmp(&camera, &previousCamera, sizeof(camera))
                                || previousSize != std::array<uint32_t, 2>{width, height};
            const auto now = example::FrameMeter::Clock::now();
            if (meter.frames && (motion || changed))
                interactionUntil = now + std::chrono::milliseconds(220);
            usingPreview   = adaptive && now < interactionUntil && lastFullMs > 25;
            auto effective = ao;
            if (usingPreview)
            {
                effective.resolutionScale = std::min(
                  ao.resolutionScale, std::max(.125f, std::min({.25f, 240.f / float(width), 180.f / float(height)})));
                effective.samples = std::min(ao.samples, 4u);
            }
            const bool qualityChanged =
              effective.resolutionScale != applied.resolutionScale || effective.samples != applied.samples;
            if (changed || motion || qualityChanged)
            {
                view.SetCamera(camera);
                view.SetAmbientOcclusion(effective);
                previousCamera = camera;
                previousSize   = {width, height};
                applied        = effective;
                changed        = false;
                output         = {};
            }
            if (output.pixels.empty() || (ao.method == render::AOMethod::RTAO && view.GetAccumulatedSamples() < 32))
            {
                const auto  start   = example::FrameMeter::Clock::now();
                const auto& picture = view.DrawFrame(width, height);
                meter.Record(start);
                if (!usingPreview)
                    lastFullMs = meter.lastMilliseconds;
                renderWidth    = picture.width;
                renderHeight   = picture.height;
                const auto top = uint32_t(headerPixels * scale * effective.resolutionScale);
                output.width   = picture.width;
                output.height =
                  std::max(picture.height + top, uint32_t(float(framebuffer[1]) * scale * effective.resolutionScale));
                output.pixels.assign(size_t(output.width) * output.height * 4, 0);
                std::memcpy(
                  output.pixels.data() + size_t(top) * output.width * 4, picture.pixels.data(), picture.pixels.size());
            }
            window.PresentImage(output);
            if (window.DrawFrame() == render::FrameResult::Rendered)
                ++frames;
        }
        window.WaitIdle();
        std::cout << "Last AO draw: " << meter.lastMilliseconds << " ms; internal " << renderWidth << "x"
                  << renderHeight << "; P95 " << meter.P95() << " ms\n";
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
