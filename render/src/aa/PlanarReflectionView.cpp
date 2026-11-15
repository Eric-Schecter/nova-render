#include "render/PlanarReflectionView.hpp"
namespace render
{
    struct PlanarReflectionView::Impl
    {
        AntiAliasingView view;
        explicit Impl(const RendererConfig& c)
          : view(c)
        {
        }
    };
    PlanarReflectionView::PlanarReflectionView(const RendererConfig& c)
      : impl_(std::make_unique<Impl>(c))
    {
        impl_->view.ConfigureReflectionScene();
        impl_->view.SetPlanarReflections({});
        AntiAliasingSettings aa;
        aa.method = AAMethod::TAA;
        impl_->view.SetAntiAliasing(aa);
        PostProcessingSettings pp;
        pp.enabled = true;
        impl_->view.SetPostProcessing(pp);
    }
    PlanarReflectionView::~PlanarReflectionView() = default;
    void PlanarReflectionView::SetPostProcessing(const PostProcessingSettings& s)
    {
        impl_->view.SetPostProcessing(s);
    }
    PostProcessingSettings PlanarReflectionView::GetPostProcessing() const
    {
        return impl_->view.GetPostProcessing();
    }
    void PlanarReflectionView::SetPlanarReflections(const PlanarReflectionSettings& s)
    {
        impl_->view.SetPlanarReflections(s);
    }
    PlanarReflectionSettings PlanarReflectionView::GetPlanarReflections() const
    {
        return impl_->view.GetPlanarReflections();
    }
    void PlanarReflectionView::LoadEnvironment(const std::filesystem::path& p)
    {
        impl_->view.LoadReflectionEnvironment(p);
    }
    void PlanarReflectionView::UseDefaultEnvironment()
    {
        impl_->view.UseDefaultReflectionEnvironment();
    }
    void PlanarReflectionView::SetAntiAliasing(const AntiAliasingSettings& s)
    {
        impl_->view.SetAntiAliasing(s);
    }
    AntiAliasingSettings PlanarReflectionView::GetAntiAliasing() const
    {
        return impl_->view.GetAntiAliasing();
    }
    void PlanarReflectionView::SetCamera(const OrbitCamera& c)
    {
        impl_->view.SetCamera(c);
    }
    OrbitCamera PlanarReflectionView::GetCamera() const
    {
        return impl_->view.GetCamera();
    }
    void PlanarReflectionView::SetTime(float t)
    {
        impl_->view.SetTime(t);
    }
    void PlanarReflectionView::ResetHistory()
    {
        impl_->view.ResetHistory();
    }
    uint32_t PlanarReflectionView::GetHistoryFrames() const
    {
        return impl_->view.GetHistoryFrames();
    }
    const Image& PlanarReflectionView::DrawFrame(uint32_t w, uint32_t h)
    {
        return impl_->view.DrawFrame(w, h);
    }
} // namespace render
