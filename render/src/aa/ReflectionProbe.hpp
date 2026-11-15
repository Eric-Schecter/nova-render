#pragma once
#include "ScreenSpaceReflections.hpp"
namespace render::detail
{
    inline void ValidateProbe(const ReflectionProbeSettings& s)
    {
        if (
          !std::isfinite(s.roughness) || s.roughness < 0 || s.roughness > 1 || !std::isfinite(s.intensity)
          || s.intensity < 0 || s.intensity > 2 || !std::isfinite(s.blendDistance) || s.blendDistance < 0
          || s.blendDistance > 2 || unsigned(s.debug) > 3)
            throw std::invalid_argument("Invalid probe settings");
        for (int i = 0; i < 3; i++)
            if (
              !std::isfinite(s.position[i]) || !std::isfinite(s.boxMin[i]) || !std::isfinite(s.boxMax[i])
              || s.boxMin[i] < -20 || s.boxMax[i] > 20 || s.boxMax[i] - s.boxMin[i] < .25f
              || s.position[i] <= s.boxMin[i] + .01f || s.position[i] >= s.boxMax[i] - .01f)
                throw std::invalid_argument("Probe must lie inside finite box");
    }
    inline void PackProbeUniforms(AAUniforms& u, const ReflectionProbeSettings& s)
    {
        u[8]     = {s.enabled ? 1.f : 0.f, -2, s.boxProjection ? 1.f : 0.f, s.roughness};
        u[9]     = {s.intensity, s.blendDistance, float(s.debug), 0};
        u[10][1] = 0;
        u[10][2] = 7161;
        for (int i = 0; i < 3; i++)
        {
            u[11][i] = s.position[i];
            u[12][i] = s.boxMin[i];
            u[13][i] = s.boxMax[i];
        }
    }
    inline void AppendProbePasses(
      std::vector<AAPass>& passes, const AAUniforms& u, bool dirty, bool ms, bool taa, bool split)
    {
        if (dirty)
        {
            AAPass p;
            p.program    = 21;
            p.target     = AAProbeCubemap;
            p.uniforms   = u;
            p.sources[3] = AASsrEnvironment;
            passes.push_back(p);
        }
        ScreenSpaceReflectionSettings off;
        off.enabled = false;
        AppendReflectionPasses(passes, u, off, ms, taa, split);
        for (auto& p : passes)
            if (p.program == 20)
                p.sources[6] = AAProbeCubemap;
    }
} // namespace render::detail
