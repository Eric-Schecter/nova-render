#include "render/PostProcessingView.hpp"
namespace render
{
    struct PostProcessingView::Impl
    {
        AntiAliasingView view;
        explicit Impl(const RendererConfig& c)
          : view(c)
        {
        }
    };
    PostProcessingView::PostProcessingView(const RendererConfig& c)
      : impl_(std::make_unique<Impl>(c))
    {
        impl_->view.ConfigureHDRScene();
        AntiAliasingSettings aa;
        aa.method = AAMethod::TAA;
        impl_->view.SetAntiAliasing(aa);
        PostProcessingSettings pp;
        pp.enabled = true;
        impl_->view.SetPostProcessing(pp);
    }
    PostProcessingView::~PostProcessingView() = default;
    void PostProcessingView::SetPostProcessing(const PostProcessingSettings& s)
    {
        impl_->view.SetPostProcessing(s);
    }
    PostProcessingSettings PostProcessingView::GetPostProcessing() const
    {
        return impl_->view.GetPostProcessing();
    }
    void PostProcessingView::SetSceneIntensity(float v)
    {
        impl_->view.ConfigureSceneIntensity(v);
    }
    float PostProcessingView::GetSceneIntensity() const
    {
        return impl_->view.GetSceneIntensity();
    }
    void PostProcessingView::SetAntiAliasing(const AntiAliasingSettings& s)
    {
        impl_->view.SetAntiAliasing(s);
    }
    AntiAliasingSettings PostProcessingView::GetAntiAliasing() const
    {
        return impl_->view.GetAntiAliasing();
    }
    void PostProcessingView::SetCamera(const OrbitCamera& c)
    {
        impl_->view.SetCamera(c);
    }
    OrbitCamera PostProcessingView::GetCamera() const
    {
        return impl_->view.GetCamera();
    }
    void PostProcessingView::SetTime(float t)
    {
        impl_->view.SetTime(t);
    }
    void PostProcessingView::ResetHistory()
    {
        impl_->view.ResetHistory();
    }
    uint32_t PostProcessingView::GetHistoryFrames() const
    {
        return impl_->view.GetHistoryFrames();
    }
    const Image& PostProcessingView::DrawFrame(uint32_t w, uint32_t h)
    {
        return impl_->view.DrawFrame(w, h);
    }
} // namespace render
