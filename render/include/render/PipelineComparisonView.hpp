#pragma once
#include "RasterPipeline.hpp"
#include "Renderer.hpp"
namespace render
{
    enum class PipelineDebug
    {
        Final,
        BaseColor,
        WorldNormal,
        LinearDepth,
        Roughness,
        Metallic,
        LightCount,
        Overflow
    };
    struct PipelineComparisonSettings
    {
        RasterPipeline pipeline       = RasterPipeline::Forward;
        PipelineDebug  debug          = PipelineDebug::Final;
        uint32_t       lightCount     = 128;
        float          lightRadius    = 3;
        float          lightIntensity = 18;
    };
    // Independent, opaque raster laboratory. Does not change Renderer RenderMode.
    // GPU-generated compact light lists, 16x9 tiles, 16 logarithmic depth slices.
    // Cells over 64 lights fall back to evaluating all lights, preserving image correctness.
    class PipelineComparisonView
    {
      public:
        explicit PipelineComparisonView(const RendererConfig& config = {});
        ~PipelineComparisonView();
        PipelineComparisonView(const PipelineComparisonView&)               = delete;
        PipelineComparisonView&    operator=(const PipelineComparisonView&) = delete;
        void                       SetSettings(const PipelineComparisonSettings& settings);
        PipelineComparisonSettings GetSettings() const;
        void                       SetCamera(const OrbitCamera& camera);
        OrbitCamera                GetCamera() const;
        const Image&               DrawFrame(uint32_t width, uint32_t height);

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
