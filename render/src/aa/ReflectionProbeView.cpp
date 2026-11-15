#include "render/ReflectionProbeView.hpp"
namespace render
{
    struct ReflectionProbeView::Impl
    {
        AntiAliasingView view;
        explicit Impl(const RendererConfig& c)
          : view(c)
        {
        }
    };
    ReflectionProbeView::ReflectionProbeView(const RendererConfig& c)
      : impl_(std::make_unique<Impl>(c))
    {
        impl_->view.ConfigureReflectionScene();
        impl_->view.SetReflectionProbe({});
        AntiAliasingSettings aa;
        aa.method = AAMethod::TAA;
        impl_->view.SetAntiAliasing(aa);
        PostProcessingSettings pp;
        pp.enabled = true;
        impl_->view.SetPostProcessing(pp);
    }
    ReflectionProbeView::~ReflectionProbeView() = default;
    void ReflectionProbeView::SetPostProcessing(const PostProcessingSettings& s)
    {
        impl_->view.SetPostProcessing(s);
    }
    PostProcessingSettings ReflectionProbeView::GetPostProcessing() const
    {
        return impl_->view.GetPostProcessing();
    }
    void ReflectionProbeView::SetReflectionProbe(const ReflectionProbeSettings& s)
    {
        impl_->view.SetReflectionProbe(s);
    }
    ReflectionProbeSettings ReflectionProbeView::GetReflectionProbe() const
    {
        return impl_->view.GetReflectionProbe();
    }
    void ReflectionProbeView::UpdateProbe()
    {
        impl_->view.UpdateProbe();
    }
    uint32_t ReflectionProbeView::GetProbeCaptureCount() const
    {
        return impl_->view.GetProbeCaptureCount();
    }
    void ReflectionProbeView::LoadEnvironment(const std::filesystem::path& p)
    {
        impl_->view.LoadReflectionEnvironment(p);
    }
    void ReflectionProbeView::UseDefaultEnvironment()
    {
        impl_->view.UseDefaultReflectionEnvironment();
    }
    void ReflectionProbeView::SetAntiAliasing(const AntiAliasingSettings& s)
    {
        impl_->view.SetAntiAliasing(s);
    }
    AntiAliasingSettings ReflectionProbeView::GetAntiAliasing() const
    {
        return impl_->view.GetAntiAliasing();
    }
    void ReflectionProbeView::SetCamera(const OrbitCamera& c)
    {
        impl_->view.SetCamera(c);
    }
    OrbitCamera ReflectionProbeView::GetCamera() const
    {
        return impl_->view.GetCamera();
    }
    void ReflectionProbeView::SetTime(float t)
    {
        impl_->view.SetTime(t);
    }
    void ReflectionProbeView::ResetHistory()
    {
        impl_->view.ResetHistory();
    }
    uint32_t ReflectionProbeView::GetHistoryFrames() const
    {
        return impl_->view.GetHistoryFrames();
    }
    const Image& ReflectionProbeView::DrawFrame(uint32_t w, uint32_t h)
    {
        return impl_->view.DrawFrame(w, h);
    }
} // namespace render
