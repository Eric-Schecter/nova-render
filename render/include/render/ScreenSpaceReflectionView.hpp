#pragma once
#include "AntiAliasingView.hpp"
#include "ScreenSpaceReflections.hpp"
namespace render
{
    // Screen-space reflection showcase, with HDR surface data, min/max Hi-Z and environment fallback.
    class ScreenSpaceReflectionView
    {
      public:
        explicit ScreenSpaceReflectionView(const RendererConfig& config = {});
        ~ScreenSpaceReflectionView();
        ScreenSpaceReflectionView(const ScreenSpaceReflectionView&)               = delete;
        ScreenSpaceReflectionView&    operator=(const ScreenSpaceReflectionView&) = delete;
        void                          SetScreenSpaceReflections(const ScreenSpaceReflectionSettings& settings);
        ScreenSpaceReflectionSettings GetScreenSpaceReflections() const;
        void                          LoadEnvironment(const std::filesystem::path& path);
        void                          UseDefaultEnvironment();
        void                          SetPostProcessing(const PostProcessingSettings& settings);
        PostProcessingSettings        GetPostProcessing() const;
        void                          SetAntiAliasing(const AntiAliasingSettings& settings);
        AntiAliasingSettings          GetAntiAliasing() const;
        void                          SetCamera(const OrbitCamera& camera);
        OrbitCamera                   GetCamera() const;
        void                          SetTime(float seconds);
        void                          ResetHistory();
        uint32_t                      GetHistoryFrames() const;
        const Image&                  DrawFrame(uint32_t width, uint32_t height);

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
