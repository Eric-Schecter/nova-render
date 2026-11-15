#pragma once
#include "AntiAliasingView.hpp"
#include "PlanarReflections.hpp"
namespace render
{
    // Single-bounce planar reflection showcase: floor and vertical mirror.
    class PlanarReflectionView
    {
      public:
        explicit PlanarReflectionView(const RendererConfig& config = {});
        ~PlanarReflectionView();
        PlanarReflectionView(const PlanarReflectionView&)               = delete;
        PlanarReflectionView&    operator=(const PlanarReflectionView&) = delete;
        void                     SetPlanarReflections(const PlanarReflectionSettings& settings);
        PlanarReflectionSettings GetPlanarReflections() const;
        void                     LoadEnvironment(const std::filesystem::path& path);
        void                     UseDefaultEnvironment();
        void                     SetPostProcessing(const PostProcessingSettings& settings);
        PostProcessingSettings   GetPostProcessing() const;
        void                     SetAntiAliasing(const AntiAliasingSettings& settings);
        AntiAliasingSettings     GetAntiAliasing() const;
        void                     SetCamera(const OrbitCamera& camera);
        OrbitCamera              GetCamera() const;
        void                     SetTime(float seconds);
        void                     ResetHistory();
        uint32_t                 GetHistoryFrames() const;
        const Image&             DrawFrame(uint32_t width, uint32_t height);

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
