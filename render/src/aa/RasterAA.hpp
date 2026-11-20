#pragma once
#include "AADevice.hpp"
#include "ModelData.hpp"
#include "PostProcessing.hpp"
#include "render/Hybrid.hpp"
namespace render::detail
{
    class RasterAA
    {
      public:
        std::unique_ptr<AADevice> device;
        uint32_t                  frames = 0, width = 0, height = 0;
        SceneUniforms             current{}, previous{};
        std::vector<JointMatrix>  previousJoints;
        std::array<float, 2>      lastJitter{};
        Image                     image;
        bool                      gpuPresentation = false, lastGPUFrame = false;
        PostProcessingState       postState;
        void                      Reset()
        {
            frames     = 0;
            lastJitter = {};
        }
        void Clear()
        {
            device.reset();
            postState.Reset();
            Reset();
            width = height = 0;
            previousJoints.clear();
        }
        const Image& Render(
          uint32_t                      w,
          uint32_t                      h,
          const AntiAliasingSettings&   requestedSettings,
          const std::array<float, 4>&   clear,
          bool                          triangle         = false,
          const PostProcessingSettings& requestedPost    = {},
          const RasterSettings&         modelSettings    = {},
          bool                          transparent      = false,
          bool                          hybrid           = false,
          bool                          hybridDebug      = false,
          const HybridSettings&         hybridSettings   = {},
          size_t                        shadowLightCount = 0)
        {
            auto settings     = requestedSettings;
            auto postSettings = requestedPost;
            if (modelSettings.view != GBufferView::Final || hybridDebug)
            {
                settings.method      = AAMethod::Off;
                settings.split       = false;
                postSettings.enabled = false;
            }
            const bool reconstruct = hybrid && hybridSettings.temporalReconstruction && !hybridDebug
                                     && modelSettings.view == GBufferView::Final;
            const bool deferred = modelSettings.pipeline == RasterPipeline::Deferred;
            if (w != width || h != height)
            {
                width  = w;
                height = h;
                Reset();
            }
            if (!frames)
                previous = current;
            auto halton = [](uint32_t i, uint32_t base) {
                float f = 1, v = 0;
                while (i)
                {
                    f /= base;
                    v += f * (i % base);
                    i /= base;
                }
                return v;
            };
            bool                 taa   = settings.method == AAMethod::TAA;
            uint32_t             phase = frames % 8 + 1;
            std::array<float, 2> jitter =
              taa ? std::array<float, 2>{halton(phase, 2) - .5f, halton(phase, 3) - .5f} : std::array<float, 2>{};
            AAUniforms u{};
            u[0]    = {float(w), float(h), 0, float(settings.method)};
            u[1]    = {jitter[0], jitter[1], lastJitter[0], lastJitter[1]};
            u[3]    = {frames ? 1.f : 0.f, settings.historyWeight, settings.zoom, settings.split ? 1.f : 0.f};
            u[5]    = {1, 1, 0, 0};
            u[6][2] = 1;
            u[7]    = clear;
            std::vector<AAPass> passes;
            auto                add = [&](int program, int target) {
                AAPass p;
                p.program  = program;
                p.target   = target;
                p.uniforms = u;
                passes.push_back(p);
                return &passes.back();
            };
            auto crop = [&](int target, int source, uint32_t activeW, uint32_t activeH, bool luma) {
                auto* p        = add(1, target);
                p->sources[0]  = source;
                p->uniforms[5] = {
                  float(activeW) / (activeW + 2),
                  float(activeH) / (activeH + 2),
                  1.f / (activeW + 2),
                  1.f / (activeH + 2)};
                p->uniforms[6][0] = luma ? 1.f : 0.f;
            };
            if (settings.split)
            {
                add(-1, AARawBaseline)->uniforms[1] = {};
                crop(AABaseline, AARawBaseline, w, h, true);
            }
            const bool oit        = transparent && modelSettings.view == GBufferView::Final;
            const int  sceneColor = oit ? AARawOpaque : AARawColor;
            const bool halfReflections = hybrid && hybridSettings.reflections
                                         && hybridSettings.halfResolutionReflections;
            const bool denoiseReflections = deferred && halfReflections && hybridSettings.reflectionDenoising;
            const int  shadowGroups       = int((shadowLightCount + 3) / 4);
            const bool temporalShadows    = shadowGroups > 0 && deferred && hybrid && hybridSettings.shadows
                                         && hybridSettings.shadowTemporalReconstruction && !hybridDebug
                                         && modelSettings.view == GBufferView::Final;
            if (deferred)
            {
                if (denoiseReflections || temporalShadows)
                    add(-2, AARawMotion);
                add(-4, AAModelBase);
                if (hybrid)
                {
                    auto* shadow = add(-7, AAHybridShadow0);
                    for (int i = 0; i < 4; i++)
                        shadow->sources[i] = AAModelBase + i;
                    shadow->sources[8] = AAModelClearcoat;
                }
                if (halfReflections)
                {
                    auto* reflection = add(-8, AAHybridReflection);
                    for (int i = 0; i < 4; i++)
                        reflection->sources[i] = AAModelBase + i;
                    reflection->sources[8] = AAModelClearcoat;
                    reflection->uniforms[1][0] *= .5f;
                    reflection->uniforms[1][1] *= .5f;
                }
                int resolvedReflection = AAHybridReflection;
                if (denoiseReflections)
                {
                    const bool odd = frames % 2;
                    resolvedReflection = odd ? AAReflectionHistoryB : AAReflectionHistoryA;
                    auto* resolve = add(30, resolvedReflection);
                    resolve->sources[0] = AAHybridReflection;
                    resolve->sources[1] = AAModelNormal;
                    resolve->sources[2] = AAModelBase;
                    resolve->sources[3] = odd ? AAReflectionHistoryA : AAReflectionHistoryB;
                    resolve->sources[4] = AARawMotion;
                    resolve->sources[5] = AAReflectionPreviousGuide;
                    resolve->sources[6] = AAModelPosition;
                    if (hybridSettings.clearcoatReflections)
                    {
                        resolve->sources[7] = AAModelClearcoat;
                        resolve->uniforms[2][0] = 1;
                    }
                    if (hybridSettings.reflectionHitReprojection)
                    {
                        resolve->uniforms[2][2] = 1;
                        for (int i = 0; i < 4; ++i)
                            std::copy_n(previous.viewProjection + i * 4, 4, resolve->uniforms[7 + i].begin());
                        std::copy_n(current.eyeExposure, 4, resolve->uniforms[11].begin());
                        std::copy_n(current.cameraRight, 4, resolve->uniforms[12].begin());
                        std::copy_n(current.cameraUp, 4, resolve->uniforms[13].begin());
                        std::copy_n(current.cameraForward, 4, resolve->uniforms[14].begin());
                        std::copy_n(previous.eyeExposure, 4, resolve->uniforms[15].begin());
                    }
                    // The raw target is free once the temporal resolve has read it.
                    auto* spatial = add(30, AAHybridReflection);
                    spatial->sources[0] = resolvedReflection;
                    spatial->sources[1] = AAModelNormal;
                    spatial->sources[2] = AAModelBase;
                    spatial->sources[4] = AARawMotion;
                    spatial->sources[6] = AAModelPosition;
                    spatial->uniforms[2][1] = 1;
                    if (hybridSettings.clearcoatReflections)
                    {
                        spatial->sources[7] = AAModelClearcoat;
                        spatial->uniforms[2][0] = 1;
                    }
                    resolvedReflection = AAHybridReflection;
                    auto* guide = add(30, AAReflectionPreviousGuide);
                    guide->sources[1] = AAModelNormal;
                    guide->sources[4] = AARawMotion;
                    guide->sources[6] = AAModelPosition;
                    guide->uniforms[6][3] = 1;
                    if (hybridSettings.clearcoatReflections)
                    {
                        guide->sources[7] = AAModelClearcoat;
                        guide->uniforms[2][0] = 1;
                    }
                }
                int resolvedShadows = AAHybridShadow0;
                if (temporalShadows)
                {
                    const int previousShadows = frames % 2 ? AAShadowHistoryA : AAShadowHistoryB;
                    resolvedShadows           = frames % 2 ? AAShadowHistoryB : AAShadowHistoryA;
                    const float scale         = settings.method == AAMethod::SSAA ? 2.f : 1.f;
                    for (int i = 0; i < shadowGroups; ++i)
                    {
                        auto* resolve           = add(7, resolvedShadows + i);
                        resolve->sources[0]     = AAHybridShadow0 + i;
                        resolve->sources[1]     = AAModelNormal;
                        resolve->sources[2]     = AAShadowPreviousNormal;
                        resolve->sources[3]     = previousShadows + i;
                        resolve->sources[4]     = AARawMotion;
                        resolve->sources[5]     = AAShadowPreviousMotion;
                        resolve->sources[7]     = AAShadowPreviousDepth;
                        resolve->uniforms[2][0] = scale;
                        resolve->uniforms[6][3] = 3;
                    }
                    add(1, AAShadowPreviousDepth)->sources[0]  = AAModelPosition;
                    add(1, AAShadowPreviousNormal)->sources[0] = AAModelNormal;
                    add(1, AAShadowPreviousMotion)->sources[0] = AARawMotion;
                }
                auto* p = add(-5, sceneColor);
                if (halfReflections)
                    p->sources[9] = resolvedReflection;
                if (hybrid)
                    for (int i = 0; i < 4; i++)
                        p->sources[4 + i] = (i < shadowGroups ? resolvedShadows : AAHybridShadow0) + i;
                for (int i = 0; i < 4; i++)
                    p->sources[i] = AAModelBase + i;
                p->sources[8] = AAModelClearcoat;
            }
            else
                add(-1, sceneColor)->multisampled = settings.method == AAMethod::MSAA;
            if (oit)
            {
                add(-6, AAOitAccum)->sources[0] = AARawOpaque;
            }
            if (oit && !reconstruct)
            {
                auto* p       = add(29, AARawColor);
                p->sources[0] = AARawOpaque;
                p->sources[1] = AAOitAccum;
                p->sources[2] = AAOitReveal;
            }
            crop(
              AAColor,
              oit && reconstruct ? AARawOpaque : AARawColor,
              w * (settings.method == AAMethod::SSAA ? 2 : 1),
              h * (settings.method == AAMethod::SSAA ? 2 : 1),
              true);
            int filtered = AAColor;
            if (taa)
            {
                add(triangle ? -3 : -2, AARawMotion);
                crop(AAMotion, AARawMotion, w, h, false);
                filtered                         = frames % 2 ? AAHistoryB : AAHistoryA;
                auto* p                          = add(7, filtered);
                p->sources[0]                    = AAColor;
                p->sources[3]                    = frames % 2 ? AAHistoryA : AAHistoryB;
                p->sources[4]                    = AAMotion;
                p->sources[5]                    = AAPrevMotion;
                add(1, AAPrevMotion)->sources[0] = AAMotion;
            }
            if (reconstruct)
            {
                if (!denoiseReflections && !temporalShadows)
                    add(-2, AARawMotion);
                const uint32_t scale = settings.method == AAMethod::SSAA ? 2 : 1;
                crop(AAMotion, AARawMotion, w * scale, h * scale, false);
                crop(AAHybridCurrentNormal, AAModelNormal, w * scale, h * scale, false);
                crop(AAHybridCurrentDepth, AAModelPosition, w * scale, h * scale, false);
                filtered                                   = frames % 2 ? AAHistoryB : AAHistoryA;
                auto* p                                    = add(7, filtered);
                p->sources[0]                              = AAColor;
                p->sources[1]                              = AAHybridCurrentNormal;
                p->sources[2]                              = AAHybridPreviousNormal;
                p->sources[3]                              = frames % 2 ? AAHistoryA : AAHistoryB;
                p->sources[4]                              = AAMotion;
                p->sources[5]                              = AAPrevMotion;
                p->sources[6]                              = AAHybridCurrentDepth;
                p->sources[7]                              = AAHybridPreviousDepth;
                p->uniforms[6][3]                          = 2;
                p->uniforms[3][1]                          = hybridSettings.historyWeight;
                add(1, AAPrevMotion)->sources[0]           = AAMotion;
                add(1, AAHybridPreviousNormal)->sources[0] = AAHybridCurrentNormal;
                add(1, AAHybridPreviousDepth)->sources[0]  = AAHybridCurrentDepth;
                if (oit)
                {
                    crop(AAHybridOitAccum, AAOitAccum, w * scale, h * scale, false);
                    crop(AAHybridOitReveal, AAOitReveal, w * scale, h * scale, false);
                    auto* composite       = add(29, AAColor);
                    composite->sources[0] = filtered;
                    composite->sources[1] = AAHybridOitAccum;
                    composite->sources[2] = AAHybridOitReveal;
                    filtered              = AAColor;
                }
            }
            const auto post = AppendPostProcessing(
              passes, filtered, settings.split ? AABaseline : AADummy, w, h, postSettings, postState, u);
            filtered = post.color;
            if (settings.method == AAMethod::FXAA)
            {
                add(2, AAFiltered)->sources[0] = filtered;
                filtered                       = AAFiltered;
            }
            if (settings.method == AAMethod::SMAA)
            {
                add(3, AAGamma)->sources[0] = filtered;
                add(4, AAEdges)->sources[0] = AAGamma;
                auto* p                     = add(5, AAWeights);
                p->sources[1]               = AAEdges;
                p->sources[6]               = AAArea;
                p->sources[7]               = AASearch;
                p                           = add(6, AAFiltered);
                p->sources[0]               = filtered;
                p->sources[2]               = AAWeights;
                filtered                    = AAFiltered;
            }
            auto* p       = add(8, AAOutput);
            p->sources[0] = filtered;
            if (settings.split)
                p->sources[3] = post.baseline;
            device->Resize(w, h, settings.method == AAMethod::SSAA);
            lastGPUFrame = gpuPresentation;
            image        = device->Draw(passes, !gpuPresentation);
            lastJitter = jitter;
            previous   = current;
            frames++;
            return image;
        }
    };
} // namespace render::detail
