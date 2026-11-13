#include "Backend.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

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
        std::cout << "Backend: " << GraphicsAPIName(api) << '\n';
    }
    Renderer::~Renderer() = default;
    void Renderer::Resize(uint32_t width, uint32_t height)
    {
        impl_->backend->Resize(width, height);
    }
    FrameResult Renderer::DrawFrame()
    {
        return impl_->backend->DrawFrame();
    }
    void Renderer::LoadModel(const std::filesystem::path& path)
    {
        auto data = detail::LoadGLB(path);
        impl_->backend->LoadModel(std::move(data));
    }
    OrbitCamera Renderer::GetCamera() const
    {
        return impl_->backend->GetCamera();
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
    void Renderer::LoadEnvironment(const std::filesystem::path& path)
    {
        impl_->backend->LoadEnvironment(detail::LoadEnvironmentFile(path));
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
    void Renderer::SetCamera(const OrbitCamera& camera)
    {
        detail::ValidateCamera(camera);
        impl_->backend->SetCamera(camera);
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
} // namespace render
