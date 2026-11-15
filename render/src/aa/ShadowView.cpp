#include "render/ShadowView.hpp"
namespace render
{
    struct ShadowView::Impl
    {
        AntiAliasingView view;
        ShadowSettings   settings;
        explicit Impl(const RendererConfig& c)
          : view(c)
        {
        }
    };
    ShadowView::ShadowView(const RendererConfig& c)
      : impl_(std::make_unique<Impl>(c))
    {
        AntiAliasingSettings aa;
        aa.method = AAMethod::TAA;
        impl_->view.SetAntiAliasing(aa);
        impl_->view.ConfigureShadow(impl_->settings);
        auto camera     = impl_->view.GetCamera();
        camera.yaw      = 28;
        camera.pitch    = 30;
        camera.distance = 8;
        impl_->view.SetCamera(camera);
    }
    ShadowView::~ShadowView() = default;
    void ShadowView::SetPostProcessing(const PostProcessingSettings& s)
    {
        impl_->view.SetPostProcessing(s);
    }
    PostProcessingSettings ShadowView::GetPostProcessing() const
    {
        return impl_->view.GetPostProcessing();
    }
    void ShadowView::SetShadowSettings(const ShadowSettings& value)
    {
        impl_->view.ConfigureShadow(value);
        impl_->settings = value;
    }
    ShadowSettings ShadowView::GetShadowSettings() const
    {
        return impl_->settings;
    }
    void ShadowView::SetAntiAliasing(const AntiAliasingSettings& value)
    {
        impl_->view.SetAntiAliasing(value);
    }
    AntiAliasingSettings ShadowView::GetAntiAliasing() const
    {
        return impl_->view.GetAntiAliasing();
    }
    void ShadowView::SetCamera(const OrbitCamera& value)
    {
        impl_->view.SetCamera(value);
    }
    OrbitCamera ShadowView::GetCamera() const
    {
        return impl_->view.GetCamera();
    }
    void ShadowView::SetTime(float value)
    {
        impl_->view.SetTime(value);
    }
    void ShadowView::ResetHistory()
    {
        impl_->view.ResetHistory();
    }
    uint32_t ShadowView::GetHistoryFrames() const
    {
        return impl_->view.GetHistoryFrames();
    }
    const Image& ShadowView::DrawFrame(uint32_t w, uint32_t h)
    {
        return impl_->view.DrawFrame(w, h);
    }
} // namespace render
