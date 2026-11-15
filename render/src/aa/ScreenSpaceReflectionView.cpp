#include "render/ScreenSpaceReflectionView.hpp"
namespace render
{
    struct ScreenSpaceReflectionView::Impl
    {
        AntiAliasingView view;
        explicit Impl(const RendererConfig& c)
          : view(c)
        {
        }
    };
    ScreenSpaceReflectionView::ScreenSpaceReflectionView(const RendererConfig& c)
      : impl_(std::make_unique<Impl>(c))
    {
        impl_->view.ConfigureReflectionScene();
        AntiAliasingSettings aa;
        aa.method = AAMethod::TAA;
        impl_->view.SetAntiAliasing(aa);
        PostProcessingSettings pp;
        pp.enabled = true;
        impl_->view.SetPostProcessing(pp);
    }
    ScreenSpaceReflectionView::~ScreenSpaceReflectionView() = default;
    void ScreenSpaceReflectionView::SetPostProcessing(const PostProcessingSettings& s)
    {
        impl_->view.SetPostProcessing(s);
    }
    PostProcessingSettings ScreenSpaceReflectionView::GetPostProcessing() const
    {
        return impl_->view.GetPostProcessing();
    }
    void ScreenSpaceReflectionView::SetScreenSpaceReflections(const ScreenSpaceReflectionSettings& s)
    {
        impl_->view.SetScreenSpaceReflections(s);
    }
    ScreenSpaceReflectionSettings ScreenSpaceReflectionView::GetScreenSpaceReflections() const
    {
        return impl_->view.GetScreenSpaceReflections();
    }
    void ScreenSpaceReflectionView::LoadEnvironment(const std::filesystem::path& p)
    {
        impl_->view.LoadReflectionEnvironment(p);
    }
    void ScreenSpaceReflectionView::UseDefaultEnvironment()
    {
        impl_->view.UseDefaultReflectionEnvironment();
    }
    void ScreenSpaceReflectionView::SetAntiAliasing(const AntiAliasingSettings& s)
    {
        impl_->view.SetAntiAliasing(s);
    }
    AntiAliasingSettings ScreenSpaceReflectionView::GetAntiAliasing() const
    {
        return impl_->view.GetAntiAliasing();
    }
    void ScreenSpaceReflectionView::SetCamera(const OrbitCamera& c)
    {
        impl_->view.SetCamera(c);
    }
    OrbitCamera ScreenSpaceReflectionView::GetCamera() const
    {
        return impl_->view.GetCamera();
    }
    void ScreenSpaceReflectionView::SetTime(float t)
    {
        impl_->view.SetTime(t);
    }
    void ScreenSpaceReflectionView::ResetHistory()
    {
        impl_->view.ResetHistory();
    }
    uint32_t ScreenSpaceReflectionView::GetHistoryFrames() const
    {
        return impl_->view.GetHistoryFrames();
    }
    const Image& ScreenSpaceReflectionView::DrawFrame(uint32_t w, uint32_t h)
    {
        return impl_->view.DrawFrame(w, h);
    }
} // namespace render
