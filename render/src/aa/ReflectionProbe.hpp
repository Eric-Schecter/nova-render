#pragma once
#include "ScreenSpaceReflections.hpp"
namespace render::detail
{
    inline std::vector<ReflectionProbeVolume> EffectiveProbes(const ReflectionProbeSettings& s)
    {
        if (!s.probes.empty())
            return s.probes;
        ReflectionProbeVolume v;
        v.position = s.position;
        v.boxMin = v.influenceMin = s.boxMin;
        v.boxMax = v.influenceMax = s.boxMax;
        v.blendDistance           = s.blendDistance;
        return {v};
    }
    AATextureData ReflectionProbeMetadata(const ReflectionProbeSettings& s);
    inline void   ValidateProbe(const ReflectionProbeSettings& s)
    {
        if (
          !std::isfinite(s.roughness) || s.roughness < 0 || s.roughness > 1 || !std::isfinite(s.intensity)
          || s.intensity < 0 || s.intensity > 2 || !std::isfinite(s.blendDistance) || s.blendDistance < 0
          || s.blendDistance > 2 || unsigned(s.debug) > 3)
            throw std::invalid_argument("Invalid probe settings");
        const auto volumes = EffectiveProbes(s);
        if (volumes.size() > 4 || s.debugProbe >= volumes.size())
            throw std::invalid_argument("Probe count must be 1..4 and debug index in range");
        for (const auto& v : volumes)
        {
            if (
              !std::isfinite(v.intensity) || v.intensity < 0 || v.intensity > 2 || !std::isfinite(v.blendDistance)
              || v.blendDistance < 0 || v.blendDistance > 2)
                throw std::invalid_argument("Invalid probe volume");
            for (int i = 0; i < 3; i++)
            {
                if (
                  !std::isfinite(v.position[i]) || !std::isfinite(v.boxMin[i]) || !std::isfinite(v.boxMax[i])
                  || !std::isfinite(v.influenceMin[i]) || !std::isfinite(v.influenceMax[i]) || v.boxMin[i] < -20
                  || v.boxMax[i] > 20 || v.boxMax[i] - v.boxMin[i] < .25f || v.influenceMin[i] < -20
                  || v.influenceMax[i] > 20 || v.influenceMax[i] - v.influenceMin[i] < .25f
                  || v.position[i] <= v.boxMin[i] + .01f || v.position[i] >= v.boxMax[i] - .01f)
                    throw std::invalid_argument("Invalid probe box or position");
            }
        }
    }
    inline void PackProbeUniforms(AAUniforms& u, const ReflectionProbeSettings& s)
    {
        u[8]     = {s.enabled ? 1.f : 0.f, -2, s.boxProjection ? 1.f : 0.f, s.roughness};
        u[9]     = {s.intensity, s.smoothBlending ? 1.f : 0.f, float(s.debug), float(EffectiveProbes(s).size())};
        u[10][1] = 0;
        u[10][0] = float(s.debugProbe);
        u[10][2] = float(7053 + 108 * EffectiveProbes(s).size());
        for (int i = 0; i < 3; i++)
        {
            u[11][i] = s.position[i];
            u[12][i] = s.boxMin[i];
            u[13][i] = s.boxMax[i];
        }
    }
    inline void AppendProbePasses(
      std::vector<AAPass>&           passes,
      const AAUniforms&              u,
      const ReflectionProbeSettings& settings,
      const std::array<bool, 4>&     dirty,
      bool                           ms,
      bool                           taa,
      bool                           split)
    {
        const auto volumes = EffectiveProbes(settings);
        for (uint32_t i = 0; i < volumes.size(); i++)
            if (dirty[i])
            {
                AAPass p;
                p.program    = 21;
                p.target     = AAProbeCubemap + i;
                p.uniforms   = u;
                p.sources[3] = AASsrEnvironment;
                for (int axis = 0; axis < 3; axis++)
                    p.uniforms[11][axis] = volumes[i].position[axis];
                passes.push_back(p);
            }
        ScreenSpaceReflectionSettings off;
        off.enabled = false;
        AppendReflectionPasses(passes, u, off, ms, taa, split);
        for (auto& p : passes)
        {
            if (p.program == 17 || p.program == 20)
                p.sources[10] = AAProbeData;
            if (p.program == 20)
                for (uint32_t i = 0; i < volumes.size(); i++)
                    p.sources[6 + i] = AAProbeCubemap + i;
        }
    }
} // namespace render::detail
