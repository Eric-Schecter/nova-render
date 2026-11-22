#pragma once
#include "Animation.hpp"
#include "Atmosphere.hpp"
#include "Hybrid.hpp"
#include "Image.hpp"
#include "Material.hpp"
#include "PostProcessing.hpp"
#include "RenderMode.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "SceneShadows.hpp"
#include <string>

namespace render
{
    struct WindowConfig
    {
        uint32_t       width  = 800;
        uint32_t       height = 600;
        std::string    title  = "Nova Render";
        RendererConfig renderer;
    };

    // Convenience entry point: owns window, event system and renderer.
    // Main thread only; one live Window, without a separately managed window host, at a time.
    class Window
    {
      public:
        explicit Window(const WindowConfig& config = {});
        ~Window();
        Window(const Window&)            = delete;
        Window& operator=(const Window&) = delete;
        // Clear a close request after a caller chooses to keep the window open.
        void CancelClose();
        // Logical window size. The framebuffer can be larger on high-density displays.
        void                    SetSize(uint32_t width, uint32_t height);
        bool                    PollEvents(); // false after closing the window or pressing Escape.
        std::array<uint32_t, 2> GetFramebufferSize() const;
        FrameResult             DrawFrame(); // Updates framebuffer size; yields briefly if skipped.
        MaterialCapabilities    GetMaterialCapabilities() const;
        void                    WaitIdle();
        void                    SetPostProcessing(const PostProcessingSettings& settings);
        PostProcessingSettings  GetPostProcessing() const;
        void                    SetRasterSettings(const RasterSettings& settings);
        RasterSettings          GetRasterSettings() const;
        RasterCapabilities      GetRasterCapabilities() const;
        void                    SetAntiAliasing(const AntiAliasingSettings& settings);
        AntiAliasingSettings    GetAntiAliasing() const;
        uint32_t                GetAntiAliasingHistoryFrames() const;

        std::vector<AnimationClip> GetAnimations() const;
        AnimationState             GetAnimationState() const;
        void                       SetAnimationState(const AnimationState& state);
        void                       UpdateAnimation(float deltaSeconds);
        std::vector<AnimationClip> GetAnimations(const std::string& assetId) const;
        AnimationState             GetAnimationState(const std::string& instanceId) const;
        void                       SetAnimationState(const std::string& instanceId, const AnimationState& state);
        void                       UpdateAnimations(float deltaSeconds);
        AnimationCapabilities      GetAnimationCapabilities() const;
        std::optional<Bounds>      GetSceneInstanceBounds(const std::string& instanceId) const;
        // Replace the raster scene; assets are shared by ID. Empty lists clear geometry.
        // See Scene.hpp for coordinate spaces and matrix layout. DrawFrame submits the update.
        void SetScene(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& instances);
        // Bounds in selected asset-local space; absent for unreferenced/inactive IDs.
        std::optional<Bounds> GetSceneAssetBounds(const std::string& assetId) const;
        // Synchronous CPU query of the last committed SetScene state; nullopt on miss.
        std::optional<SceneHit> PickScene(const Ray& ray) const;
        std::vector<Material>   GetMaterials(const std::string& assetId = {}) const;
        void                    SetMaterial(uint32_t slot, const Material& material, const std::string& assetId = {});
        void                    SetCloseOnEscape(bool enabled);
        void                    LoadModel(const std::filesystem::path& glbPath);
        void                    LoadModel(const std::filesystem::path& glbPath, uint32_t materialVariant);
        RendererCapabilities    GetCapabilities() const;
        void                    SetHybrid(const HybridSettings& settings);
        HybridSettings          GetHybrid() const;
        uint32_t                GetHybridHistoryFrames() const;
        void                    ResetHybridHistory();
        HybridCapabilities      GetHybridCapabilities() const;
        void                    SetRenderMode(RenderMode mode);
        RenderMode              GetRenderMode() const;
        void                    SetPathTracing(const PathTracingSettings& settings);
        PathTracingSettings     GetPathTracing() const;
        void                    SetRayTracing(const RayTracingSettings& settings) { SetPathTracing(settings); }
        RayTracingSettings      GetRayTracing() const { return GetPathTracing(); }
        void                    ResetAccumulation();
        uint32_t                GetAccumulatedSamples() const;
        void                    PresentImage(const Image& image);
        void                    ClearImage();
        void                    SetShadows(const SceneShadowSettings& settings);
        SceneShadowSettings     GetShadows() const;
        void                    SetFog(const FogSettings& settings);
        FogSettings             GetFog() const;
        void                    LoadEnvironment(const std::filesystem::path& path);
        void                    SetEnvironment(const EnvironmentSettings& settings);
        EnvironmentSettings     GetEnvironment() const;
        void                    SetDirectionalLight(const DirectionalLight& light);
        DirectionalLight        GetDirectionalLight() const;
        void                    SetLights(const std::vector<SceneLight>& lights);
        std::vector<SceneLight> GetLights() const;
        LightingCapabilities    GetLightingCapabilities() const;
        void                    SetLighting(LightingMode mode);
        // Returns false if UI was disabled at build time. Disabled by default.
        bool                    EnableParameterPanel(bool enabled = true);
        bool                    EnableParameterPanel(const ParameterPanelConfig& config);
        void                    SetClearColor(const std::array<float, 4>& color);
        std::array<float, 4>    GetClearColor() const;
        void                    SetViewCamera(const Camera& camera);
        Camera                  GetViewCamera() const;
        void                    SetViewport(const std::optional<Viewport>& viewport);
        std::optional<Viewport> GetViewport() const;
        void                    SetCamera(const OrbitCamera& camera);
        OrbitCamera             GetCamera() const;
        void                    EnableOrbitControls(bool enabled = true);
        GraphicsAPI             GetAPI() const;
        uint32_t                ValidationErrorCount() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace render
