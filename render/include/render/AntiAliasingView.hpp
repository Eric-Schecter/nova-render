#pragma once
#include "AntiAliasing.hpp"
#include "PlanarReflections.hpp"
#include "PostProcessing.hpp"
#include "ReflectionProbe.hpp"
#include "Renderer.hpp"
#include "ScreenSpaceReflections.hpp"
namespace render
{
    // Independent raster laboratory: procedural rails, fine checkerboard and moving spokes.
    // Uses genuine 4x render-target MSAA; no ray-tracing requirement or external scene asset.
    class AntiAliasingView
    {
      public:
        explicit AntiAliasingView(const RendererConfig& config = {});
        ~AntiAliasingView();
        AntiAliasingView(const AntiAliasingView&)                        = delete;
        AntiAliasingView&             operator=(const AntiAliasingView&) = delete;
        void                          SetPostProcessing(const PostProcessingSettings& value);
        PostProcessingSettings        GetPostProcessing() const;
        float                         GetSceneIntensity() const;
        void                          SetScreenSpaceReflections(const ScreenSpaceReflectionSettings& settings);
        ScreenSpaceReflectionSettings GetScreenSpaceReflections() const;
        void                          LoadReflectionEnvironment(const std::filesystem::path& path);
        void                          UseDefaultReflectionEnvironment();
        void                          SetAntiAliasing(const AntiAliasingSettings& settings);
        AntiAliasingSettings          GetAntiAliasing() const;
        void                          SetCamera(const OrbitCamera& camera);
        OrbitCamera                   GetCamera() const;
        void                          SetTime(float seconds);
        void                          ResetHistory();
        uint32_t                      GetHistoryFrames() const;
        const Image&                  DrawFrame(uint32_t width, uint32_t height);

      private:
        friend class ShadowView;
        friend class PostProcessingView;
        friend class ScreenSpaceReflectionView;
        friend class PlanarReflectionView;
        friend class ReflectionProbeView;
        void                     SetReflectionProbe(const ReflectionProbeSettings& value);
        ReflectionProbeSettings  GetReflectionProbe() const;
        void                     UpdateProbe();
        void                     UpdateProbe(uint32_t index);
        uint32_t                 GetProbeCaptureCount() const;
        void                     SetPlanarReflections(const PlanarReflectionSettings& value);
        PlanarReflectionSettings GetPlanarReflections() const;
        void                     ConfigureReflectionScene();
        void                     ConfigureHDRScene();
        void                     ConfigureSceneIntensity(float value);
        void                     ConfigureShadow(const struct ShadowSettings& value);
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
