#pragma once
#include "AntiAliasingView.hpp"
namespace render
{
    enum class ShadowMethod
    {
        Off,
        Hard,
        PCF,
        PCSS
    };
    struct ShadowSettings
    {
        ShadowMethod         method       = ShadowMethod::PCSS;
        uint32_t             resolution   = 1024;
        float                depthBias    = .001f;
        float                normalBias   = .015f;
        float                filterRadius = 2.f;
        float                lightSize    = .35f;
        std::array<float, 3> lightDirection{-1.5f, 3.f, 2.f};
    };
    // Procedural shadow showcase: real light-space raster map plus shared raster AA passes.
    class ShadowView
    {
      public:
        explicit ShadowView(const RendererConfig& config = {});
        ~ShadowView();
        ShadowView(const ShadowView&)                     = delete;
        ShadowView&          operator=(const ShadowView&) = delete;
        void                 SetShadowSettings(const ShadowSettings& value);
        ShadowSettings       GetShadowSettings() const;
        void                 SetAntiAliasing(const AntiAliasingSettings& value);
        AntiAliasingSettings GetAntiAliasing() const;
        void                 SetCamera(const OrbitCamera& value);
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
