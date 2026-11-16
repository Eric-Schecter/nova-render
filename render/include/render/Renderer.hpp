#pragma once
#include "Animation.hpp"
#include "AntiAliasing.hpp"
#include "Atmosphere.hpp"
#include "Image.hpp"
#include "Lights.hpp"
#include "Material.hpp"
#include "Model.hpp"
#include "ParameterPanel.hpp"
#include "PostProcessing.hpp"
#include "RenderMode.hpp"
#include "Scene.hpp"
#include "SceneShadows.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>

struct GLFWwindow;

namespace render
{
    enum class GraphicsAPI
    {
        Auto,
        Vulkan,
        D3D12,
        Metal
    };
    const char* GraphicsAPIName(GraphicsAPI api);

    struct RendererConfig
    {
        GraphicsAPI api              = GraphicsAPI::Auto;
        bool        enableValidation = true;
        bool        useWarp          = false; // Explicit D3D12 software device for validation.
        // Empty uses embedded triangle shaders; nonempty explicitly overrides from disk.
        std::filesystem::path shaderDirectory;
    };

    // Pure selection policy: resolves Auto, rejects unavailable backends and invalid WARP use.
    // Does not initialize a device or silently retry another API.
    bool        IsGraphicsAPIAvailable(GraphicsAPI api);
    GraphicsAPI ResolveGraphicsAPI(const RendererConfig& config);

    enum class FrameResult
    {
        Rendered,
        Skipped
    };

    // GLFW must be initialized. The window must outlive this object.
    // Use on the window's main thread; only one renderer at a time is supported.
    // Auto selects Metal on macOS, D3D12 on Windows, Vulkan on Linux.
    class Renderer
    {
      public:
        Renderer(GLFWwindow* window, const RendererConfig& config);
        ~Renderer();
        Renderer(const Renderer&)            = delete;
        Renderer& operator=(const Renderer&) = delete;

        // Supply framebuffer pixels before the first frame and after size changes.
        // Zero dimensions suspend rendering. Rebuilds happen inside DrawFrame().
        void                       Resize(uint32_t framebufferWidth, uint32_t framebufferHeight);
        FrameResult                DrawFrame();
        void                       WaitIdle();
        void                       SetPostProcessing(const PostProcessingSettings& settings);
        PostProcessingSettings     GetPostProcessing() const;
        void                       SetAntiAliasing(const AntiAliasingSettings& settings);
        AntiAliasingSettings       GetAntiAliasing() const;
        uint32_t                   GetAntiAliasingHistoryFrames() const;
        std::vector<AnimationClip> GetAnimations() const;
        AnimationState             GetAnimationState() const;
        void                       SetAnimationState(const AnimationState& state);
        void                       UpdateAnimation(float deltaSeconds);
        // Replace the static raster scene; assets are shared by ID. Empty lists clear geometry.
        // See Scene.hpp for coordinate spaces and matrix layout. DrawFrame submits the update.
        void SetScene(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& instances);
        // Bounds in selected asset-local space; absent for unreferenced/inactive IDs.
        std::optional<Bounds>      GetSceneAssetBounds(const std::string& assetId) const;
        // Synchronous CPU query of the last committed SetScene state; nullopt on miss.
        std::optional<SceneHit>    PickScene(const Ray& ray) const;
        std::vector<Material>      GetMaterials(const std::string& assetId = {}) const;
        void SetMaterial(uint32_t slot, const Material& material, const std::string& assetId = {});
        void                       LoadModel(const std::filesystem::path& glbPath);
        RendererCapabilities       GetCapabilities() const;
        void                       SetRenderMode(RenderMode mode);
        RenderMode                 GetRenderMode() const;
        void                       SetPathTracing(const PathTracingSettings& settings);
        PathTracingSettings        GetPathTracing() const;
        void                       SetRayTracing(const RayTracingSettings& settings) { SetPathTracing(settings); }
        RayTracingSettings         GetRayTracing() const { return GetPathTracing(); }
        void                       ResetAccumulation();
        uint32_t                   GetAccumulatedSamples() const;
        void                       PresentImage(const Image& image);
        void                       ClearImage();
        void                       SetShadows(const SceneShadowSettings& settings);
        SceneShadowSettings        GetShadows() const;
        void                       SetFog(const FogSettings& settings);
        FogSettings                GetFog() const;
        void                       LoadEnvironment(const std::filesystem::path& path);
        void                       SetEnvironment(const EnvironmentSettings& settings);
        EnvironmentSettings        GetEnvironment() const;
        void                       SetDirectionalLight(const DirectionalLight& light);
        DirectionalLight           GetDirectionalLight() const;
        void                       SetLights(const std::vector<SceneLight>& lights);
        std::vector<SceneLight>    GetLights() const;
        LightingCapabilities       GetLightingCapabilities() const;
        void                       SetLighting(LightingMode mode);
        // Returns false if UI was disabled at build time. Disabled by default.
        bool                 EnableParameterPanel(bool enabled = true);
        bool                 EnableParameterPanel(const ParameterPanelConfig& config);
        void                 SetClearColor(const std::array<float, 4>& color);
        std::array<float, 4> GetClearColor() const;
        void                    SetViewCamera(const Camera& camera);
        Camera                  GetViewCamera() const;
        void                    SetViewport(const std::optional<Viewport>& viewport);
        std::optional<Viewport> GetViewport() const;
        void                 SetCamera(const OrbitCamera& camera);
        OrbitCamera          GetCamera() const;
        GraphicsAPI          GetAPI() const;
        uint32_t             ValidationErrorCount() const;

      private:
        friend class Window;
        bool UIWantsMouse() const;
        bool UIWantsKeyboard() const;
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
