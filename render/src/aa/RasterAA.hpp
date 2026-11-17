#pragma once
#include "AADevice.hpp"
#include "ModelData.hpp"
#include "PostProcessing.hpp"
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
          bool                          triangle      = false,
          const PostProcessingSettings& requestedPost = {},
          const RasterSettings&         modelSettings = {},
          bool                          transparent   = false,
          bool                          hybrid        = false,
          bool                          hybridDebug   = false)
        {
            auto settings     = requestedSettings;
            auto postSettings = requestedPost;
            if (modelSettings.view != GBufferView::Final || hybridDebug)
            {
                settings.method      = AAMethod::Off;
                settings.split       = false;
                postSettings.enabled = false;
            }
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
            if (deferred)
            {
                add(-4, AAModelBase);
                if (hybrid)
                {
                    auto* shadow = add(-7, AAHybridShadow0);
                    for (int i = 0; i < 4; i++)
                        shadow->sources[i] = AAModelBase + i;
                }
                auto* p = add(-5, sceneColor);
                if (hybrid)
                    for (int i = 0; i < 4; i++)
                        p->sources[4 + i] = AAHybridShadow0 + i;
                for (int i = 0; i < 4; i++)
                    p->sources[i] = AAModelBase + i;
            }
            else
                add(-1, sceneColor)->multisampled = settings.method == AAMethod::MSAA;
            if (oit)
            {
                add(-6, AAOitAccum);
                auto* p       = add(29, AARawColor);
                p->sources[0] = AARawOpaque;
                p->sources[1] = AAOitAccum;
                p->sources[2] = AAOitReveal;
            }
            crop(
              AAColor,
              AARawColor,
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
