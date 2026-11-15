#pragma once
#include "AntiAliasingView.hpp"
#include "PostProcessing.hpp"
namespace render
{
    // HDR room with emissive lamps and material objects, using the common post-processing chain.
    class PostProcessingView
    {
      public:
        explicit PostProcessingView(const RendererConfig& config = {});
        ~PostProcessingView();
        PostProcessingView(const PostProcessingView&)               = delete;
        PostProcessingView&    operator=(const PostProcessingView&) = delete;
        void                   SetPostProcessing(const PostProcessingSettings& settings);
        PostProcessingSettings GetPostProcessing() const;
        void                   SetSceneIntensity(float intensity);
        float                  GetSceneIntensity() const;
        void                   SetAntiAliasing(const AntiAliasingSettings& settings);
        AntiAliasingSettings   GetAntiAliasing() const;
        void                   SetCamera(const OrbitCamera& camera);
        OrbitCamera            GetCamera() const;
        void                   SetTime(float seconds);
        void                   ResetHistory();
        uint32_t               GetHistoryFrames() const;
        const Image&           DrawFrame(uint32_t width, uint32_t height);

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
