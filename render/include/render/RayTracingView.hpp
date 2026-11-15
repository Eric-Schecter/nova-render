#pragma once
#include "AmbientOcclusion.hpp"
#include "Image.hpp"
#include "Renderer.hpp"
namespace render
{
    // Independent offscreen accumulation, using the same shared HLSL as Window.
    // Owns its device; does not own a window, UI, layout or sampling budget.
    class RayTracingView
    {
      public:
        explicit RayTracingView(const RendererConfig& config = {});
        ~RayTracingView();
        RayTracingView(const RayTracingView&)                     = delete;
        RayTracingView&          operator=(const RayTracingView&) = delete;
        RendererCapabilities     GetCapabilities() const;
        void                     LoadModel(const std::filesystem::path& path);
        void                     SetCamera(const OrbitCamera& camera);
        OrbitCamera              GetCamera() const;
        void                     SetDirectionalLight(const DirectionalLight& light);
        DirectionalLight         GetDirectionalLight() const;
        void                     LoadEnvironment(const std::filesystem::path& path);
        void                     SetEnvironment(const EnvironmentSettings& settings);
        EnvironmentSettings      GetEnvironment() const;
        void                     SetRayTracing(const RayTracingSettings& settings);
        RayTracingSettings       GetRayTracing() const;
        void                     SetAmbientOcclusion(const AmbientOcclusionSettings& settings);
        AmbientOcclusionSettings GetAmbientOcclusion() const;
        void                     ResetAccumulation();
        uint32_t                 GetAccumulatedSamples() const;
        const Image&             DrawFrame(uint32_t width, uint32_t height);

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
