#include "../common/FloatingPanel.hpp"
#include "../common/PerformancePanel.hpp"
#include "../common/ProbeVolumes.hpp"
#include "../common/ProceduralOptions.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <render/ReflectionProbeView.hpp>
#ifdef NOVA_EXAMPLE_UI
#include <imgui.h>
#endif
int main(int argc, char** argv)
{
    try
    {
        auto options          = example::ParseProceduralOptions(argc, argv, "Nova | Reflection probes");
        options.window.width  = 1100;
        options.window.height = 900;
        render::Window              window(options.window);
        render::ReflectionProbeView view(options.window.renderer);
        auto                        settings    = view.GetAntiAliasing();
        auto                        post        = view.GetPostProcessing();
        auto                        reflections = view.GetReflectionProbe();
        int                         environment = 0, pendingEnvironment = -1, selectedProbe = 0;
        reflections.probes = example::RoomProbes();
        view.SetReflectionProbe(reflections);
        auto camera = view.GetCamera();
        window.SetCamera(camera);
        window.EnableOrbitControls();
        example::PerformancePanel    performance;
        bool                         playing = true, changed = true, reset = false, cameraChanged = false;
        float                        scale = .75f, speed = .6f, time = 0;
        int                          settle  = 32;
        const char*                  names[] = {"Off", "FXAA", "SMAA 1x", "MSAA 4x", "SSAA 2x2", "TAA"};
        render::ParameterPanelConfig panel;
        panel.showBuiltinPanel = false;
#ifdef NOVA_EXAMPLE_UI
        panel.drawUI = [&] {
            example::PlaceFloatingPanel();
            ImGui::Begin("Reflection probes", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            if (ImGui::BeginTabBar("Reflection controls"))
            {
                if (ImGui::BeginTabItem("Reflection probe"))
                {
                    if (ImGui::BeginTable("Probe controls", 1, ImGuiTableFlags_SizingStretchSame))
                    {
                        ImGui::TableNextColumn();
                        changed |= ImGui::Checkbox("Local cubemap", &reflections.enabled);
                        changed |= ImGui::Checkbox("Box projection", &reflections.boxProjection);
                        changed |= ImGui::SliderFloat("Roughness", &reflections.roughness, 0, 1);
                        changed |= ImGui::SliderFloat("Reflection intensity", &reflections.intensity, 0, 2);
                        changed |= ImGui::Checkbox("Smooth blending", &reflections.smoothBlending);
                        if (ImGui::Combo("Active probe", &selectedProbe, "Red\0Green\0Blue\0"))
                        {
                            reflections.debugProbe = uint32_t(selectedProbe);
                            changed                = true;
                        }
                        auto& volume = reflections.probes[size_t(selectedProbe)];
                        changed |= ImGui::Checkbox("Probe enabled", &volume.enabled);
                        changed |= ImGui::SliderFloat("Boundary blend", &volume.blendDistance, 0, 2);
                        changed |= ImGui::SliderFloat("Probe intensity", &volume.intensity, 0, 2);
                        ImGui::TableNextColumn();
                        changed |= ImGui::SliderFloat("Probe X", &volume.position[0], -3, 3);
                        changed |= ImGui::SliderFloat("Probe Y", &volume.position[1], -.5f, 2.5f);
                        changed |= ImGui::SliderFloat("Probe Z", &volume.position[2], -3, 3);
                        if (ImGui::Button("Refresh probe"))
                        {
                            view.UpdateProbe(uint32_t(selectedProbe));
                            changed = true;
                        }
                        ImGui::Text("Captures: %u", view.GetProbeCaptureCount());
                        ImGui::TableNextColumn();
                        int debug = int(reflections.debug);
                        if (ImGui::Combo("Output", &debug, "Composite\0Reflections\0Influence\0Cubemap\0"))
                        {
                            reflections.debug = render::ProbeDebug(debug);
                            changed           = true;
                        }
                        if (ImGui::Combo(
                              "Environment",
                              &environment,
                              "Studio (procedural HDR)\0Venice sunset\0Kloppenheim\0Studio small\0"))
                            pendingEnvironment = environment;
                        ImGui::TextUnformatted("Red / Green / Blue probe regions");
                        changed |= ImGui::SliderFloat("Travel phase", &time, 0, 6.28318f);
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Post-processing"))
                {
                    if (ImGui::BeginTable("Post controls", 1, ImGuiTableFlags_SizingStretchSame))
                    {
                        ImGui::TableNextColumn();
                        changed |= ImGui::Checkbox("Post-processing", &post.enabled);
                        int tone = int(post.toneMapping);
                        if (ImGui::Combo("Tone mapping", &tone, "Linear\0Reinhard\0ACES fit\0"))
                        {
                            post.toneMapping = render::ToneMapping(tone);
                            changed          = true;
                        }
                        changed |= ImGui::Checkbox("Auto exposure", &post.autoExposure);
                        changed |= ImGui::SliderFloat("Exposure / EV", &post.exposureEV, -8, 8);
                        changed |= ImGui::SliderFloat("Adaptation rate", &post.adaptationRate, .1f, 10);
                        ImGui::TableNextColumn();
                        if (ImGui::SliderFloat("Min auto EV", &post.minEV, -10, 10))
                        {
                            post.maxEV = std::max(post.minEV, post.maxEV);
                            changed    = true;
                        }
                        if (ImGui::SliderFloat("Max auto EV", &post.maxEV, -10, 10))
                        {
                            post.minEV = std::min(post.minEV, post.maxEV);
                            changed    = true;
                        }
                        changed |= ImGui::Checkbox("Bloom", &post.bloom);
                        changed |= ImGui::SliderFloat("Bloom intensity", &post.bloomIntensity, 0, 2);
                        changed |= ImGui::SliderFloat("Bloom threshold", &post.bloomThreshold, 0, 20);
                        ImGui::TableNextColumn();
                        changed |= ImGui::SliderFloat("Bloom soft knee", &post.bloomKnee, 0, 1);
                        changed |= ImGui::SliderFloat("Saturation", &post.saturation, 0, 2);
                        changed |= ImGui::SliderFloat("Contrast", &post.contrast, .1f, 2);
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("AA and playback"))
                {
                    int method = int(settings.method);
                    if (ImGui::Combo("AA", &method, names, 6))
                    {
                        settings.method = render::AAMethod(method);
                        changed         = true;
                    }
                    changed |= ImGui::Checkbox("Animate", &playing);
                    changed |= ImGui::SliderFloat("Speed", &speed, .05f, 1);
                    changed |= ImGui::SliderFloat("Render scale", &scale, .25f, 1);
                    ImGui::BeginDisabled(settings.method != render::AAMethod::TAA);
                    changed |= ImGui::SliderFloat("History weight", &settings.historyWeight, 0, .98f);
                    if (ImGui::Button("Reset history"))
                        reset = true;
                    ImGui::EndDisabled();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        };
#endif
        performance.Attach(panel);
        window.EnableParameterPanel(panel);
        auto          settleUntil = example::FrameMeter::Clock::now();
        auto          previous    = example::FrameMeter::Clock::now();
        int           frames      = 0;
        render::Image composed;
        std::string   metricMethod;
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
            if (pendingEnvironment >= 0)
            {
                const char* names[] = {"", "venice_sunset.nenv", "kloppenheim_06.nenv", "studio_small_09.nenv"};
                if (pendingEnvironment == 0)
                    view.UseDefaultEnvironment();
                else
                    view.LoadEnvironment(std::filesystem::path(NOVA_ENVIRONMENT_DIR) / names[pendingEnvironment]);
                pendingEnvironment = -1;
                changed            = true;
            }
            if (changed)
            {
                view.SetAntiAliasing(settings);
                view.SetPostProcessing(post);
                view.SetReflectionProbe(reflections);
                if (post.enabled && post.autoExposure)
                    settleUntil = start + std::chrono::milliseconds(int(8000 / post.adaptationRate));
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
            const bool update = changed || playing || (settings.method == render::AAMethod::TAA && settle > 0)
                                || (post.enabled && post.autoExposure && start < settleUntil);
            changed = false;
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
            performance.active = playing || (settings.method == render::AAMethod::TAA && settle > 0)
                                 || (post.enabled && post.autoExposure && start < settleUntil);
            const uint32_t ss  = settings.method == render::AAMethod::SSAA ? 2 : 1;
            performance.width  = w * ss;
            performance.height = h * ss;
            performance.scale  = scale * limit * ss;
            performance.samples =
              settings.method == render::AAMethod::MSAA || settings.method == render::AAMethod::SSAA ? 4 : 1;
            const char* toneNames[] = {"Linear", "Reinhard", "ACES fit"};
            metricMethod =
              std::string(reflections.boxProjection ? "Probe | Box projection | " : "Probe | Uncorrected | ")
              + std::string(post.enabled ? toneNames[unsigned(post.toneMapping)] : "Bypass") + " | "
              + (post.autoExposure ? "Auto exposure | " : "Manual exposure | ") + names[int(settings.method)];
            performance.method      = metricMethod.c_str();
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
