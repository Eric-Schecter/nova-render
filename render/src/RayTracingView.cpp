#include "render/RayTracingView.hpp"
#include "path/PathTracing.hpp"
namespace render
{
    struct RayTracingView::Impl
    {
        detail::PathSession                            session;
        RendererCapabilities                           caps;
        std::shared_ptr<const detail::ModelData>       model;
        OrbitCamera                                    camera;
        DirectionalLight                               light;
        std::shared_ptr<const detail::EnvironmentData> environment;
        EnvironmentSettings                            environmentSettings;
        RayTracingSettings                             settings;
        AmbientOcclusionSettings                       ao;
    };
    RayTracingView::RayTracingView(const RendererConfig& config)
      : impl_(std::make_unique<Impl>())
    {
        impl_->caps            = impl_->session.Capabilities(ResolveGraphicsAPI(config), config);
        impl_->caps.rayTracing = impl_->caps.pathTracing;
    }
    RayTracingView::~RayTracingView() = default;
    RendererCapabilities RayTracingView::GetCapabilities() const
    {
        return impl_->caps;
    }
    void RayTracingView::LoadModel(const std::filesystem::path& path)
    {
        auto model = detail::LoadGLB(path);
        if (detail::HasAlpha(model.get()))
            throw std::invalid_argument("Alpha materials currently require raster rendering");
        if (model->animation)
            throw std::invalid_argument("Offscreen ray tracing requires a static model");
        impl_->model = std::move(model);
        impl_->session.Reset();
    }
    void RayTracingView::SetCamera(const OrbitCamera& camera)
    {
        // Use the same validation as the window renderer.
        detail::ValidateCamera(camera);
        impl_->camera = camera;
    }
    OrbitCamera RayTracingView::GetCamera() const
    {
        return impl_->camera;
    }
    void RayTracingView::SetDirectionalLight(const DirectionalLight& light)
    {
        detail::ValidateDirectionalLight(light);
        impl_->light = light;
    }
    DirectionalLight RayTracingView::GetDirectionalLight() const
    {
        return impl_->light;
    }
    void RayTracingView::LoadEnvironment(const std::filesystem::path& path)
    {
        auto environment   = detail::LoadEnvironmentFile(path);
        impl_->environment = std::move(environment);
        impl_->session.Reset();
    }
    void RayTracingView::SetEnvironment(const EnvironmentSettings& settings)
    {
        detail::ValidateEnvironment(settings);
        impl_->environmentSettings = settings;
    }
    EnvironmentSettings RayTracingView::GetEnvironment() const
    {
        return impl_->environmentSettings;
    }
    RayTracingSettings RayTracingView::GetRayTracing() const
    {
        return impl_->settings;
    }
    void RayTracingView::SetRayTracing(const RayTracingSettings& settings)
    {
        detail::ValidatePathTracing(settings);
        if (settings.integrator == RayIntegrator::Metropolis && !impl_->caps.metropolis)
            throw std::runtime_error("MLT requires a supported GPU; DX12 WARP is not supported");
        impl_->settings = settings;
        impl_->session.Reset();
    }
    void RayTracingView::SetAmbientOcclusion(const AmbientOcclusionSettings& value)
    {
        detail::ValidateAmbientOcclusion(value);
        impl_->ao = value;
        impl_->session.Reset();
    }
    AmbientOcclusionSettings RayTracingView::GetAmbientOcclusion() const
    {
        return impl_->ao;
    }
    void RayTracingView::ResetAccumulation()
    {
        impl_->session.Reset();
    }
    uint32_t RayTracingView::GetAccumulatedSamples() const
    {
        return impl_->session.Samples();
    }
    const Image& RayTracingView::DrawFrame(uint32_t width, uint32_t height)
    {
        if (!impl_->caps.rayTracing)
            throw std::runtime_error(impl_->caps.reason);
        if (!impl_->model)
            throw std::invalid_argument("Load a model before drawing");
        if (!width || !height || width > 8192 || height > 8192)
            throw std::invalid_argument("View dimensions require 1..8192");
        auto scene = detail::MakeSceneUniforms(
          *impl_->model,
          impl_->camera,
          float(width) / height,
          LightingMode::PBR,
          impl_->light,
          impl_->environment.get(),
          impl_->environmentSettings);
        return impl_->session.Render(
          impl_->model, impl_->environment, scene, impl_->settings, {0, 0, 0, 1}, width, height, &impl_->ao);
    }
} // namespace render
