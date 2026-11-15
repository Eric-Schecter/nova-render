#pragma once
#include "FrameMeter.hpp"
#include <array>
#include <cstdio>
#include <functional>
#include <render/Window.hpp>
#ifdef NOVA_EXAMPLE_UI
#include <imgui.h>
#endif
namespace example
{
    // Example UI only. Completion waits make Draw comparable to the browser's GPU-completed frames.
    class PerformancePanel
    {
      public:
        FrameMeter  meter;
        uint32_t    width = 0, height = 0, samples = 1, accumulated = 0;
        float       scale  = 1;
        const char* method = "Raster";
        bool        active = true;
        void        Attach(render::ParameterPanelConfig& config)
        {
#ifdef NOVA_EXAMPLE_UI
            config.sceneTopInset = 40;
            const auto previous  = config.drawUI;
            config.drawUI        = [this, previous] {
                DrawUI();
                if (previous)
                    previous();
            };
#endif
        }
        template <class Target>
        render::FrameResult DrawFrame(Target& target, std::array<uint32_t, 2> size)
        {
            const auto start  = FrameMeter::Clock::now();
            const auto result = target.DrawFrame();
            if (result == render::FrameResult::Rendered)
            {
                target.WaitIdle();
                meter.Record(start);
                const auto path                      = target.GetRayTracing();
                const bool ray                       = target.GetRenderMode() != render::RenderMode::Raster;
                scale                                = ray ? path.resolutionScale : 1;
                samples                              = ray ? path.samplesPerFrame : 1;
                accumulated                          = ray ? target.GetAccumulatedSamples() : 0;
                static constexpr const char* names[] = {"PT", "BDPT", "Photon mapping", "MLT"};
                method                               = ray ? names[static_cast<unsigned>(path.integrator)] : "Raster";
                if (!ray)
                {
                    const auto                   aa        = target.GetAntiAliasing();
                    static constexpr const char* aaNames[] = {
                      "Raster", "FXAA", "SMAA 1x", "MSAA 4x", "SSAA 2x2", "TAA"};
                    method      = aaNames[unsigned(aa.method)];
                    scale       = aa.method == render::AAMethod::SSAA ? 2 : 1;
                    samples     = aa.method == render::AAMethod::SSAA || aa.method == render::AAMethod::MSAA ? 4 : 1;
                    accumulated = target.GetAntiAliasingHistoryFrames();
                }
                float inset = 0;
#ifdef NOVA_EXAMPLE_UI
                inset = 40 * ImGui::GetIO().DisplayFramebufferScale.y;
#endif
                width  = std::max(1u, uint32_t(size[0] * scale));
                height = std::max(1u, uint32_t(std::max(1.f, float(size[1]) - inset) * scale));
            }
            else
            {
                active = false;
                meter.FPS(false);
            }
            if (result == render::FrameResult::Rendered)
                active = true;
            return result;
        }
        void DrawUI()
        {
#ifdef NOVA_EXAMPLE_UI
            const auto size    = ImGui::GetIO().DisplaySize;
            char       fps[32] = "--", p95[32] = "--", line[256];
            if (const auto value = meter.FPS(active); value > 0)
                std::snprintf(fps, sizeof(fps), "%.1f", value);
            if (meter.frames > 1)
                std::snprintf(p95, sizeof(p95), "%.1f", meter.P95());
            auto* draw = ImGui::GetForegroundDrawList();
            draw->AddRectFilled({0, 0}, {size.x, 40}, IM_COL32(21, 27, 38, 255));
            std::snprintf(line, sizeof(line), "%s FPS | Draw %.1f ms | P95 %s ms", fps, meter.lastMilliseconds, p95);
            draw->AddText({8, 3}, IM_COL32_WHITE, line);
            std::snprintf(
              line,
              sizeof(line),
              "%s | %ux%u | %.1f%% | %s | %u samples | %u accumulated",
              active ? "Rendering" : "Idle",
              width,
              height,
              scale * 100,
              method,
              samples,
              accumulated);
            draw->AddText({8, 21}, IM_COL32_WHITE, line);
#endif
        }
    };
} // namespace example
