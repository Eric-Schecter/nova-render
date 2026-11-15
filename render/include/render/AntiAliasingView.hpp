#pragma once
#include "Renderer.hpp"
namespace render
{
    enum class AAMethod
    {
        Off,
        FXAA,
        SMAA,
        MSAA,
        SSAA,
        TAA
    };
    struct AntiAliasingSettings
    {
        AAMethod method        = AAMethod::TAA;
        float    historyWeight = .9f;
        float    zoom          = 1;
        bool     split         = true;
    };
    // Independent raster laboratory: procedural rails, fine checkerboard and moving spokes.
    // Uses genuine 4x render-target MSAA; no ray-tracing requirement or external scene asset.
    class AntiAliasingView
    {
      public:
        explicit AntiAliasingView(const RendererConfig& config = {});
        ~AntiAliasingView();
        AntiAliasingView(const AntiAliasingView&)               = delete;
        AntiAliasingView&    operator=(const AntiAliasingView&) = delete;
        void                 SetAntiAliasing(const AntiAliasingSettings& settings);
        AntiAliasingSettings GetAntiAliasing() const;
        void                 SetCamera(const OrbitCamera& camera);
        OrbitCamera          GetCamera() const;
        void                 SetTime(float seconds);
        void                 ResetHistory();
        uint32_t             GetHistoryFrames() const;
        const Image&         DrawFrame(uint32_t width, uint32_t height);

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
