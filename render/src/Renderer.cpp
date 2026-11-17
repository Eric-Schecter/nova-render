#include "Backend.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace render
{
    const char* GraphicsAPIName(GraphicsAPI api)
    {
        switch (api)
        {
        case GraphicsAPI::Auto: return "Auto";
        case GraphicsAPI::Vulkan: return "Vulkan";
        case GraphicsAPI::D3D12: return "D3D12";
        case GraphicsAPI::Metal: return "Metal";
        }
        return "Unknown";
    }
    bool IsGraphicsAPIAvailable(GraphicsAPI api)
    {
        switch (api)
        {
        case GraphicsAPI::Auto: return true;
#ifdef NOVA_HAS_VULKAN
        case GraphicsAPI::Vulkan: return true;
#endif
#ifdef __APPLE__
        case GraphicsAPI::Metal: return true;
#endif
#ifdef _WIN32
        case GraphicsAPI::D3D12: return true;
#endif
        default: return false;
        }
    }
    GraphicsAPI ResolveGraphicsAPI(const RendererConfig& config)
    {
        auto api = config.api;
        if (api == GraphicsAPI::Auto)
        {
#ifdef __APPLE__
            api = GraphicsAPI::Metal;
#elif defined(_WIN32)
            api = GraphicsAPI::D3D12;
#else
            api = GraphicsAPI::Vulkan;
#endif
        }
        if (config.useWarp && api != GraphicsAPI::D3D12)
            throw std::invalid_argument("WARP requires the D3D12 backend");
        if (!IsGraphicsAPIAvailable(api))
            throw std::runtime_error(
              std::string(GraphicsAPIName(api)) + " backend is unavailable in this build/platform");
        return api;
    }
    struct Renderer::Impl
    {
        GLFWwindow*                      window = nullptr;
        GraphicsAPI                      api;
        std::unique_ptr<detail::Backend> backend;
    };
    Renderer::Renderer(GLFWwindow* window, const RendererConfig& config)
      : impl_(std::make_unique<Impl>())
    {
        impl_->window  = window;
        const auto api = ResolveGraphicsAPI(config);
        if (!window)
            throw std::invalid_argument("Renderer requires a GLFW window");
        std::cout << "Selected backend: " << GraphicsAPIName(api) << " (requested: " << GraphicsAPIName(config.api)
                  << ")\n";
        impl_->api = api;
        switch (api)
        {
#ifdef NOVA_HAS_VULKAN
        case GraphicsAPI::Vulkan: impl_->backend = detail::CreateVulkanBackend(window, config); break;
#endif
#ifdef __APPLE__
        case GraphicsAPI::Metal: impl_->backend = detail::CreateMetalBackend(window, config); break;
#endif
#ifdef _WIN32
        case GraphicsAPI::D3D12: impl_->backend = detail::CreateD3D12Backend(window, config); break;
#endif
        default:
            throw std::runtime_error(
              std::string(GraphicsAPIName(api)) + " backend is unavailable in this build/platform");
        }
        impl_->backend->ConfigurePathTracing(api, config);
        std::cout << "Backend: " << GraphicsAPIName(api) << '\n';
    }
    Renderer::~Renderer() = default;
    RendererCapabilities Renderer::GetCapabilities() const
    {
        auto caps       = impl_->backend->GetCapabilities();
        caps.rayTracing = caps.pathTracing;
        return caps;
    }
    void Renderer::SetRenderMode(RenderMode mode)
    {
        impl_->backend->SetRenderMode(mode);
    }
    RenderMode Renderer::GetRenderMode() const
    {
        return impl_->backend->GetRenderMode();
    }
    void Renderer::SetPathTracing(const PathTracingSettings& value)
    {
        impl_->backend->SetPathTracing(value);
    }
    PathTracingSettings Renderer::GetPathTracing() const
    {
        return impl_->backend->GetPathTracing();
    }
    void Renderer::ResetAccumulation()
    {
        impl_->backend->ResetAccumulation();
    }
    void Renderer::PresentImage(const Image& image)
    {
        impl_->backend->PresentImage(image);
    }
    void Renderer::ClearImage()
    {
        impl_->backend->ClearImage();
    }
    uint32_t Renderer::GetAccumulatedSamples() const
    {
        return impl_->backend->GetAccumulatedSamples();
    }
    void Renderer::Resize(uint32_t width, uint32_t height)
    {
        impl_->backend->Resize(width, height);
    }
    FrameResult Renderer::DrawFrame()
    {
        auto& assets = impl_->backend->assets;
        if (assets.requestedMode)
        {
            const auto mode = std::exchange(assets.requestedMode, std::nullopt);
            try
            {
                SetRenderMode(*mode);
                assets.error.clear();
            }
            catch (const std::exception& error)
            {
                assets.error = error.what();
            }
        }
        if (std::exchange(assets.requestedReset, false))
            ResetAccumulation();
        auto apply = [&](auto& request, auto load) {
            if (!request)
                return;
            auto path = std::exchange(request, std::nullopt);
            try
            {
                load(*path);
                assets.error.clear();
            }
            catch (const std::exception& error)
            {
                assets.error = error.what();
            }
        };
        // Apply UI requests before a backend starts recording the next frame.
        apply(assets.requestedModel, [&](const auto& path) { LoadModel(path); });
        apply(assets.requestedEnvironment, [&](const auto& path) { LoadEnvironment(path); });
        return impl_->backend->DrawFrame();
    }
    std::vector<AnimationClip> Renderer::GetAnimations() const
    {
        return impl_->backend->animation.Clips();
    }
    AnimationState Renderer::GetAnimationState() const
    {
        return impl_->backend->animation.state;
    }
    void Renderer::SetAnimationState(const AnimationState& value)
    {
        impl_->backend->SetAnimationState(value);
    }
    void Renderer::UpdateAnimation(float delta)
    {
        impl_->backend->animation.Update(delta);
    }
    std::optional<Bounds> Renderer::GetSceneAssetBounds(const std::string& id) const
    {
        return impl_->backend->scene.active ? impl_->backend->scene.AssetBounds(id) : std::nullopt;
    }
    std::optional<SceneHit> Renderer::PickScene(const Ray& ray) const
    {
        return impl_->backend->scene.Pick(ray);
    }
    void Renderer::SetScene(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& instances)
    {
        impl_->backend->SetScene(assets, instances);
    }
    std::vector<Material> Renderer::GetMaterials(const std::string& id) const
    {
        return impl_->backend->GetMaterials(id);
    }
    void Renderer::SetMaterial(uint32_t slot, const Material& value, const std::string& id)
    {
        impl_->backend->SetMaterial(slot, value, id);
    }
    void Renderer::LoadModel(const std::filesystem::path& path)
    {
        auto data = detail::LoadGLB(path);
        impl_->backend->ValidateMaterialState(data.get());
        impl_->backend->LoadModel(std::move(data));
        impl_->backend->assets.modelPath = path;
    }
    OrbitCamera Renderer::GetCamera() const
    {
        return impl_->backend->GetCamera();
    }
    void Renderer::SetLights(const std::vector<SceneLight>& lights)
    {
        impl_->backend->SetLights(lights);
    }
    std::vector<SceneLight> Renderer::GetLights() const
    {
        return impl_->backend->GetLights();
    }
    LightingCapabilities Renderer::GetLightingCapabilities() const
    {
        return impl_->backend->GetLightingCapabilities();
    }
    void Renderer::SetDirectionalLight(const DirectionalLight& light)
    {
        detail::ValidateDirectionalLight(light);
        impl_->backend->SetDirectionalLight(light);
    }
    DirectionalLight Renderer::GetDirectionalLight() const
    {
        return impl_->backend->GetDirectionalLight();
    }
    bool Renderer::EnableParameterPanel(bool enabled)
    {
        return impl_->backend->EnableParameterPanel(impl_->window, enabled);
    }
    bool Renderer::EnableParameterPanel(const ParameterPanelConfig& config)
    {
        for (const auto* choices : {&config.models, &config.environments})
            for (const auto& choice : *choices)
                if (choice.name.empty() || choice.path.empty())
                    throw std::invalid_argument("Asset choices require a name and path");
        const bool enabled = EnableParameterPanel(true);
        if (enabled)
        {
            impl_->backend->assets.config = config;
            impl_->backend->ConfigureUI();
        }
        return enabled;
    }
    bool Renderer::UIWantsMouse() const
    {
        return impl_->backend->UIWantsMouse();
    }
    bool Renderer::UIWantsKeyboard() const
    {
        return impl_->backend->UIWantsKeyboard();
    }
    void Renderer::SetClearColor(const std::array<float, 4>& color)
    {
        for (float v : color)
            if (!std::isfinite(v) || v < 0 || v > 1)
                throw std::invalid_argument("Clear color must be RGBA in [0,1]");
        impl_->backend->SetClearColor(color);
    }
    std::array<float, 4> Renderer::GetClearColor() const
    {
        return impl_->backend->GetClearColor();
    }
    void Renderer::SetLighting(LightingMode mode)
    {
        if (mode != LightingMode::Unlit && mode != LightingMode::PBR)
            throw std::invalid_argument("Invalid lighting mode");
        impl_->backend->SetLighting(mode);
    }
    void Renderer::SetShadows(const SceneShadowSettings& settings)
    {
        impl_->backend->SetShadows(settings);
    }
    SceneShadowSettings Renderer::GetShadows() const
    {
        return impl_->backend->GetShadows();
    }
    void Renderer::SetFog(const FogSettings& settings)
    {
        impl_->backend->SetFog(settings);
    }
    FogSettings Renderer::GetFog() const
    {
        return impl_->backend->GetFog();
    }
    void Renderer::LoadEnvironment(const std::filesystem::path& path)
    {
        impl_->backend->LoadEnvironment(path.empty() ? nullptr : detail::LoadEnvironmentFile(path));
        impl_->backend->assets.environmentPath = path;
    }
    void Renderer::SetEnvironment(const EnvironmentSettings& settings)
    {
        detail::ValidateEnvironment(settings);
        impl_->backend->SetEnvironment(settings);
    }
    EnvironmentSettings Renderer::GetEnvironment() const
    {
        return impl_->backend->GetEnvironment();
    }
    void Renderer::SetViewCamera(const Camera& camera)
    {
        impl_->backend->SetViewCamera(camera);
    }
    Camera Renderer::GetViewCamera() const
    {
        return impl_->backend->GetViewCamera();
    }
    void Renderer::SetViewport(const std::optional<Viewport>& viewport)
    {
        impl_->backend->SetViewport(viewport);
    }
    std::optional<Viewport> Renderer::GetViewport() const
    {
        return impl_->backend->GetViewport();
    }
    void Renderer::SetCamera(const OrbitCamera& camera)
    {
        detail::ValidateCamera(camera);
        impl_->backend->SetCamera(camera);
    }
    void Renderer::SetPostProcessing(const PostProcessingSettings& settings)
    {
        impl_->backend->SetPostProcessing(settings);
    }
    PostProcessingSettings Renderer::GetPostProcessing() const
    {
        return impl_->backend->GetPostProcessing();
    }
    MaterialCapabilities Renderer::GetMaterialCapabilities() const
    {
        return {};
    }
    void Renderer::SetRasterSettings(const RasterSettings& settings)
    {
        impl_->backend->SetRasterSettings(settings);
    }
    RasterSettings Renderer::GetRasterSettings() const
    {
        return impl_->backend->GetRasterSettings();
    }
    RasterCapabilities Renderer::GetRasterCapabilities() const
    {
        return impl_->backend->GetRasterCapabilities();
    }
    void Renderer::SetAntiAliasing(const AntiAliasingSettings& settings)
    {
        impl_->backend->SetAntiAliasing(settings);
    }
    AntiAliasingSettings Renderer::GetAntiAliasing() const
    {
        return impl_->backend->GetAntiAliasing();
    }
    uint32_t Renderer::GetAntiAliasingHistoryFrames() const
    {
        return impl_->backend->GetAntiAliasingHistoryFrames();
    }
    void Renderer::WaitIdle()
    {
        impl_->backend->WaitIdle();
    }
    GraphicsAPI Renderer::GetAPI() const
    {
        return impl_->api;
    }
    uint32_t Renderer::ValidationErrorCount() const
    {
        return impl_->backend->ValidationErrorCount();
    }
    void Renderer::SetHybrid(const HybridSettings& settings)
    {
        impl_->backend->SetHybrid(settings);
    }
    HybridSettings Renderer::GetHybrid() const
    {
        return impl_->backend->GetHybrid();
    }
    HybridCapabilities Renderer::GetHybridCapabilities() const
    {
        return impl_->backend->GetHybridCapabilities();
    }
} // namespace render
