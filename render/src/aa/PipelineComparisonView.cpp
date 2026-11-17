#include "AADevice.hpp"
#include <render/PipelineComparisonView.hpp>
namespace render
{
    struct PipelineComparisonView::Impl
    {
        std::unique_ptr<detail::AADevice> device;
        PipelineComparisonSettings        settings;
        OrbitCamera                       camera{25, 22, 19, 1, {0, 0, 0}};
        Image                             image;
    };
    PipelineComparisonView::PipelineComparisonView(const RendererConfig& config)
      : impl_(std::make_unique<Impl>())
    {
        switch (ResolveGraphicsAPI(config))
        {
#ifdef __APPLE__
        case GraphicsAPI::Metal: impl_->device = detail::CreateMetalAA(config); break;
#endif
#ifdef NOVA_HAS_VULKAN
        case GraphicsAPI::Vulkan: impl_->device = detail::CreateVulkanAA(config); break;
#endif
#ifdef _WIN32
        case GraphicsAPI::D3D12: impl_->device = detail::CreateD3D12AA(config); break;
#endif
        default: throw std::runtime_error("Raster pipeline backend unavailable");
        }
    }
    PipelineComparisonView::~PipelineComparisonView() = default;
    void PipelineComparisonView::SetSettings(const PipelineComparisonSettings& s)
    {
        if (
          unsigned(s.pipeline) > 3 || unsigned(s.debug) > 7 || s.lightCount > 256 || !std::isfinite(s.lightRadius)
          || s.lightRadius <= 0 || s.lightRadius > 32 || !std::isfinite(s.lightIntensity) || s.lightIntensity < 0
          || s.lightIntensity > 100)
            throw std::invalid_argument("Invalid pipeline comparison settings");
        impl_->settings = s;
    }
    PipelineComparisonSettings PipelineComparisonView::GetSettings() const
    {
        return impl_->settings;
    }
    void PipelineComparisonView::SetCamera(const OrbitCamera& c)
    {
        if (
          !std::isfinite(c.yaw) || !std::isfinite(c.pitch) || !std::isfinite(c.distance) || std::abs(c.pitch) > 80
          || c.distance < 4 || c.distance > 50)
            throw std::invalid_argument("Invalid pipeline camera");
        for (float v : c.target)
            if (!std::isfinite(v))
                throw std::invalid_argument("Invalid camera target");
        impl_->camera = c;
    }
    OrbitCamera PipelineComparisonView::GetCamera() const
    {
        return impl_->camera;
    }
    const Image& PipelineComparisonView::DrawFrame(uint32_t w, uint32_t h)
    {
        using namespace detail;
        if (w < 16 || h < 9 || w > 2048 || h > 2048)
            throw std::invalid_argument("Pipeline dimensions must be 16x9 to 2048x2048");
        const auto& s = impl_->settings;
        const auto& c = impl_->camera;
        AAUniforms  u{};
        u[0]     = {float(w), float(h), float(s.pipeline), float(s.debug)};
        u[1]     = {float(s.lightCount), s.lightRadius, 0, 0};
        u[2]     = {c.yaw * .01745329252f, c.pitch * .01745329252f, c.distance, 0};
        u[3]     = {c.target[0], c.target[1], c.target[2], 0};
        u[4][0]  = s.lightIntensity;
        u[7]     = {.015f, .02f, .03f, 1};
        u[10][2] = 51 * 36;
        std::vector<AAPass> passes;
        auto                append = [&](int program, int target) {
            AAPass p;
            p.program    = program;
            p.target     = target;
            p.uniforms   = u;
            p.sources[0] = AAPipelineAlbedo;
            p.sources[1] = AAPipelineNormal;
            p.sources[2] = AAPipelinePosition;
            p.sources[3] = AAPipelineMaterial;
            p.sources[4] = AAPipelineDepth;
            p.sources[5] = AAPipelineTiles;
            p.sources[6] = AAPipelineLists;
            // Avoid binding an attachment as a sampled input, even if optimized out of HLSL.
            for (int& source : p.sources)
                if (source == target || (program == 22 && source >= target && source < target + 4))
                    source = AADummy;
            passes.push_back(p);
        };
        if (s.pipeline == RasterPipeline::ForwardPlus)
        {
            append(27, AAPipelineDepth);
            append(26, AAPipelineTiles);
            append(25, AAPipelineLists);
        }
        if (s.pipeline == RasterPipeline::ClusteredForward)
            append(25, AAPipelineLists);
        if (s.pipeline == RasterPipeline::Deferred)
        {
            append(22, AAPipelineAlbedo);
            append(24, AAOutput);
        }
        else
            append(23, AAOutput);
        impl_->device->Resize(w, h, false);
        impl_->image = impl_->device->Draw(passes);
        return impl_->image;
    }
} // namespace render
