#pragma once
#include "AADevice.hpp"
#include "render/PostProcessing.hpp"
#include <chrono>
#include <stdexcept>
namespace render::detail
{
    inline void ValidatePostProcessing(const PostProcessingSettings& s)
    {
        if (
          unsigned(s.toneMapping) > unsigned(ToneMapping::AgX) || !std::isfinite(s.exposureEV) || s.exposureEV < -8 || s.exposureEV > 8
          || !std::isfinite(s.minEV) || !std::isfinite(s.maxEV) || s.minEV < -10 || s.maxEV > 10 || s.minEV > s.maxEV
          || !std::isfinite(s.adaptationRate) || s.adaptationRate < .1f || s.adaptationRate > 10
          || !std::isfinite(s.bloomThreshold) || s.bloomThreshold < 0 || s.bloomThreshold > 20
          || !std::isfinite(s.bloomKnee) || s.bloomKnee < 0 || s.bloomKnee > 1 || !std::isfinite(s.bloomIntensity)
          || s.bloomIntensity < 0 || s.bloomIntensity > 2 || !std::isfinite(s.saturation) || s.saturation < 0
          || s.saturation > 2 || !std::isfinite(s.contrast) || s.contrast < .1f || s.contrast > 2)
            throw std::invalid_argument("Invalid post-processing settings");
    }
    struct PostProcessingState
    {
        uint32_t                              frames = 0;
        std::chrono::steady_clock::time_point last{};
        void                                  Reset()
        {
            frames = 0;
            last   = {};
        }
        float Delta()
        {
            auto  now = std::chrono::steady_clock::now();
            float dt  = last.time_since_epoch().count() ? std::chrono::duration<float>(now - last).count() : 1.f / 60;
            last      = now;
            return std::clamp(dt, 1.f / 240, .1f);
        }
    };
    struct PostProcessingResult
    {
        int color, baseline;
    };
    inline PostProcessingResult AppendPostProcessing(
      std::vector<AAPass>&          passes,
      int                           color,
      int                           baseline,
      uint32_t                      w,
      uint32_t                      h,
      const PostProcessingSettings& s,
      PostProcessingState&          state,
      const AAUniforms&             base)
    {
        if (!s.enabled)
            return {color, baseline};
        AAUniforms u = base;
        u[11]        = {1, float(s.toneMapping), s.exposureEV, s.autoExposure ? 1.f : 0.f};
        u[12]        = {s.bloom ? 1.f : 0.f, s.bloomThreshold, s.bloomKnee, s.bloomIntensity};
        u[13]        = {s.saturation, s.contrast, s.adaptationRate, state.Delta()};
        u[14]        = {s.minEV, s.maxEV, base[14][2], 0};
        u[15]        = {state.frames ? 1.f : 0.f, 0, 0, 0};
        auto add     = [&](int program, int target, int source) {
            AAPass p;
            p.program    = program;
            p.target     = target;
            p.uniforms   = u;
            p.sources[0] = source;
            passes.push_back(p);
            return &passes.back();
        };
        int exposure = AADummy;
        if (s.autoExposure)
        {
            uint32_t rw = w, rh = h;
            int      input = color, level = 0;
            do
            {
                auto* p            = add(13, AALuminance0 + level, input);
                p->uniforms[15][1] = float(level);
                input              = AALuminance0 + level++;
                rw                 = (rw + 3) / 4;
                rh                 = (rh + 3) / 4;
            } while ((rw > 1 || rh > 1) && level < 6);
            exposure      = state.frames % 2 ? AAExposureB : AAExposureA;
            auto* p       = add(14, exposure, input);
            p->sources[1] = state.frames % 2 ? AAExposureA : AAExposureB;
        }
        int bloom = AADummy;
        if (s.bloom && s.bloomIntensity > 0)
        {
            int input = color;
            for (int i = 0; i < 5; i++)
            {
                auto* p            = add(11, AABloomDown0 + i, input);
                p->uniforms[15][1] = i == 0 ? 1.f : 0.f;
                input              = AABloomDown0 + i;
            }
            for (int i = 3; i >= 0; i--)
            {
                auto* p       = add(12, AABloomUp0 + i, AABloomDown0 + i);
                p->sources[1] = input;
                input         = AABloomUp0 + i;
            }
            bloom = input;
        }
        auto compose = [&](int source, int target) {
            auto* p       = add(15, target, source);
            p->sources[1] = bloom;
            p->sources[2] = exposure;
        };
        compose(color, AAPostColor);
        if (baseline != AADummy)
            compose(baseline, AAPostBaseline);
        state.frames++;
        return {AAPostColor, baseline == AADummy ? AADummy : AAPostBaseline};
    }
} // namespace render::detail
