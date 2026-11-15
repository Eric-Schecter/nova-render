#pragma once
#include "AADevice.hpp"
#include "EnvironmentData.hpp"
#include "render/ScreenSpaceReflections.hpp"
#include <stdexcept>
namespace render::detail
{
    inline void ValidateReflections(const ScreenSpaceReflectionSettings& s)
    {
        if (
          s.maxSteps < 16 || s.maxSteps > 256 || !std::isfinite(s.maxDistance) || s.maxDistance < 1
          || s.maxDistance > 60 || !std::isfinite(s.thickness) || s.thickness < .01f || s.thickness > 1
          || !std::isfinite(s.stride) || s.stride < .5f || s.stride > 4 || !std::isfinite(s.roughnessCutoff)
          || s.roughnessCutoff < .05f || s.roughnessCutoff > 1 || !std::isfinite(s.edgeFade) || s.edgeFade < .005f
          || s.edgeFade > .3f || !std::isfinite(s.intensity) || s.intensity < 0 || s.intensity > 2
          || !std::isfinite(s.environmentIntensity) || s.environmentIntensity < 0 || s.environmentIntensity > 4
          || unsigned(s.debug) > 6 || s.debugMip > 12)
            throw std::invalid_argument("Invalid screen-space reflection settings");
    }
    AATextureData ReflectionEnvironmentAtlas(const EnvironmentData* environment = nullptr);
    inline void   PackReflectionUniforms(
        AAUniforms& u, const ScreenSpaceReflectionSettings& s, uint32_t w, uint32_t h, bool ss)
    {
        uint32_t levels = 0, side = std::max(w, h) * (ss ? 2 : 1);
        while (side > 1)
        {
            side = (side + 1) / 2;
            levels++;
        }
        u[6][1] = float(levels);
        u[6][2] = 1;
        u[6][3] = 1;
        u[8]    = {s.enabled ? 1.f : 0.f, float(s.maxSteps), s.maxDistance, s.thickness};
        u[9]    = {s.stride, s.roughnessCutoff, s.edgeFade, s.hierarchical ? 1.f : 0.f};
        u[10]   = {s.intensity, float(s.debug), float(6 + 19 * 36 + 3 * 16 * 24 * 6 + 3), s.environmentIntensity};
    }
    inline void AppendReflectionPasses(
      std::vector<AAPass>&                 passes,
      const AAUniforms&                    u,
      const ScreenSpaceReflectionSettings& s,
      bool                                 ms,
      bool                                 taa,
      bool                                 split)
    {
        auto add = [&](int program, int target) {
            AAPass p;
            p.program  = program;
            p.target   = target;
            p.uniforms = u;
            passes.push_back(p);
            return &passes.back();
        };
        auto* scene         = add(17, AASsrColor);
        scene->multisampled = ms;
        scene->sources[3]   = AASsrEnvironment;
        if (s.enabled || s.debug == ReflectionDebug::DepthPyramid)
        {
            int input = AASsrDepth;
            for (uint32_t level = 0; level < uint32_t(u[6][1]); level++)
            {
                auto* p           = add(18, AASsrHiZ0 + level);
                p->sources[0]     = input;
                p->uniforms[5][0] = level == 0 ? 1.f : 0.f;
                input             = AASsrHiZ0 + level;
            }
        }
        auto bind = [&](AAPass* p) {
            p->sources[0] = AASsrColor;
            p->sources[1] = AASsrNormal;
            p->sources[2] = AASsrDepth;
            p->sources[3] = AASsrEnvironment;
        };
        if (s.enabled)
        {
            auto* trace = add(19, AASsrHit);
            bind(trace);
            for (uint32_t i = 0; i < uint32_t(u[6][1]); i++)
                trace->sources[4 + i] = AASsrHiZ0 + i;
        }
        auto* composite = add(20, AASsrComposite);
        bind(composite);
        composite->uniforms[5][2] = float(std::min(s.debugMip, uint32_t(u[6][1])));
        if (s.debug == ReflectionDebug::DepthPyramid)
        {
            composite->sources[4] =
              uint32_t(u[6][1]) == 0 || s.debugMip == 0
                ? AASsrDepth
                : AASsrHiZ0 + std::min(s.debugMip ? s.debugMip - 1 : 0, uint32_t(u[6][1]) ? uint32_t(u[6][1]) - 1 : 0);
        }
        else if (s.enabled)
            composite->sources[4] = AASsrHit;
        auto* copy           = add(1, AAColor);
        copy->sources[0]     = AASsrComposite;
        copy->uniforms[5]    = {1, 1, 0, 0};
        copy->uniforms[6][0] = taa ? 0.f : 1.f;
        if (taa)
        {
            auto* motion        = add(1, AAMotion);
            motion->sources[0]  = AASsrMotion;
            motion->uniforms[5] = {1, 1, 0, 0};
        }
        if (split)
        {
            auto* base           = add(1, AABaseline);
            base->sources[0]     = AASsrComposite;
            base->uniforms[5]    = {1, 1, 0, 0};
            base->uniforms[6][0] = 1;
        }
    }
} // namespace render::detail
