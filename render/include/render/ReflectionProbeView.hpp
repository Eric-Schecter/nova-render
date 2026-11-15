#pragma once
#include "AntiAliasingView.hpp"
#include "ReflectionProbe.hpp"
namespace render
{
    // Local HDR cubemap probe showcase with box projection and cached static capture.
    class ReflectionProbeView
    {
      public:
        explicit ReflectionProbeView(const RendererConfig& config = {});
        ~ReflectionProbeView();
        ReflectionProbeView(const ReflectionProbeView&)                          = delete;
        ReflectionProbeView&               operator=(const ReflectionProbeView&) = delete;
        void                               SetReflectionProbe(const ReflectionProbeSettings& settings);
        ReflectionProbeSettings            GetReflectionProbe() const;
        void                               UpdateProbe();
        void                               UpdateProbe(uint32_t index);
        void                               SetReflectionProbes(const std::vector<ReflectionProbeVolume>& probes);
        std::vector<ReflectionProbeVolume> GetReflectionProbes() const;
        uint32_t                           GetProbeCaptureCount() const;
        void                               LoadEnvironment(const std::filesystem::path& path);
        void                               UseDefaultEnvironment();
        void                               SetPostProcessing(const PostProcessingSettings& settings);
        PostProcessingSettings             GetPostProcessing() const;
        void                               SetAntiAliasing(const AntiAliasingSettings& settings);
        AntiAliasingSettings               GetAntiAliasing() const;
        void                               SetCamera(const OrbitCamera& camera);
        OrbitCamera                        GetCamera() const;
        void                               SetTime(float seconds);
        void                               ResetHistory();
        uint32_t                           GetHistoryFrames() const;
        const Image&                       DrawFrame(uint32_t width, uint32_t height);

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
