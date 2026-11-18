#include "render/Window.hpp"
#include "OrbitInput.hpp"
#include "PresentationWindow.hpp"
#ifdef NOVA_HAS_SDL3
#include <SDL3/SDL.h>
#endif
#ifdef NOVA_HAS_GLFW
#include <GLFW/glfw3.h>
#endif
#include <cstdlib>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace render
{
    namespace
    {
        bool windowContextActive = false; // Window's API is confined to the main thread.
        struct Context
        {
#ifdef NOVA_HAS_SDL3
            bool sdl = true;
#else
            bool sdl = false;
#endif
            Context()
            {
                if (windowContextActive)
                    throw std::runtime_error("Only one Nova Window is supported");
                if (const char* requested = std::getenv("NOVA_WINDOW_BACKEND"))
                {
                    const std::string_view name(requested);
                    if (name != "glfw" && name != "sdl3")
                        throw std::invalid_argument("NOVA_WINDOW_BACKEND must be glfw or sdl3");
                    sdl = name == "sdl3";
                }
                if (sdl)
                {
#ifdef NOVA_HAS_SDL3
                    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
                        throw std::runtime_error(SDL_GetError());
#else
                    throw std::runtime_error("SDL3 window host requires NOVA_ENABLE_SDL3=ON");
#endif
                }
                else
                {
#ifdef NOVA_HAS_GLFW
                    if (!glfwInit())
                        throw std::runtime_error("GLFW initialization failed");
#else
                    throw std::runtime_error("GLFW host requires NOVA_ENABLE_GLFW=ON");
#endif
                }
                windowContextActive = true;
            }
            ~Context()
            {
#ifdef NOVA_HAS_SDL3
                if (sdl)
                    SDL_QuitSubSystem(SDL_INIT_VIDEO);
                else
#endif
                {
#ifdef NOVA_HAS_GLFW
                    glfwTerminate();
#endif
                }
                windowContextActive = false;
            }
        };
    } // namespace
    struct Window::Impl
    {
        Context context;
#ifdef NOVA_HAS_GLFW
        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window{nullptr, glfwDestroyWindow};
#endif
#ifdef NOVA_HAS_SDL3
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> sdlWindow{nullptr, SDL_DestroyWindow};
        bool                                                      sdlClose = false;
#endif
        std::unique_ptr<Renderer> renderer;
        bool                      orbitEnabled = false, dragging = false;
        bool                      closeOnEscape = true;
        double                    lastX = 0, lastY = 0;
        std::exception_ptr        inputError;
        template <class Change>
        void UpdateCamera(Change change) noexcept
        {
            try
            {
                renderer->SetCamera(change(renderer->GetCamera()));
            }
            catch (...)
            {
                inputError = std::current_exception();
            }
        }
        ~Impl()
        {
            renderer.reset();
#ifdef NOVA_HAS_GLFW
            if (window)
            {
                glfwSetMouseButtonCallback(window.get(), nullptr);
                glfwSetCursorPosCallback(window.get(), nullptr);
                glfwSetScrollCallback(window.get(), nullptr);
                glfwSetWindowFocusCallback(window.get(), nullptr);
                glfwSetWindowUserPointer(window.get(), nullptr);
            }
#endif
        }
#ifdef NOVA_HAS_SDL3
        bool PollSDL()
        {
            SDL_Event  event;
            const auto id = SDL_GetWindowID(sdlWindow.get());
            while (SDL_PollEvent(&event))
            {
                renderer->ProcessWindowEvent(&event);
                switch (event.type)
                {
                case SDL_EVENT_QUIT: sdlClose = true; break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (event.window.windowID == id)
                        sdlClose = true;
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    if (event.window.windowID == id)
                        dragging = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (
                      event.key.windowID == id && event.key.key == SDLK_ESCAPE && !event.key.repeat && closeOnEscape
                      && !renderer->UIWantsKeyboard())
                        sdlClose = true;
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.windowID == id && event.button.button == SDL_BUTTON_LEFT)
                        dragging =
                          event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && orbitEnabled && !renderer->UIWantsMouse();
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (event.motion.windowID == id && dragging && orbitEnabled)
                        UpdateCamera(
                          [&](OrbitCamera c) { return detail::DragOrbit(c, event.motion.xrel, event.motion.yrel); });
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    if (event.wheel.windowID == id && orbitEnabled && !renderer->UIWantsMouse())
                    {
                        const float y =
                          event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
                        UpdateCamera([&](OrbitCamera c) { return detail::ZoomOrbit(c, -y * 100.0); });
                    }
                    break;
                }
            }
            return !sdlClose;
        }
#endif
        explicit Impl(const WindowConfig& config)
        {
#ifdef NOVA_HAS_SDL3
            if (context.sdl)
            {
                const bool vulkan = ResolveGraphicsAPI(config.renderer) == GraphicsAPI::Vulkan;
                sdlWindow.reset(
                  detail::CreateSDLWindow(config.title.c_str(), int(config.width), int(config.height), vulkan, true));
                if (!sdlWindow)
                    throw std::runtime_error(SDL_GetError());
                renderer.reset(new Renderer(detail::PresentationWindow(sdlWindow.get()), config.renderer));
                return;
            }
#endif
#ifdef NOVA_HAS_GLFW
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            window.reset(glfwCreateWindow(
              static_cast<int>(config.width), static_cast<int>(config.height), config.title.c_str(), nullptr, nullptr));
            if (!window)
                throw std::runtime_error("Window creation failed");
            glfwSetKeyCallback(window.get(), [](GLFWwindow* w, int key, int, int action, int) {
                if (
                  key == GLFW_KEY_ESCAPE && action == GLFW_PRESS
                  && static_cast<Impl*>(glfwGetWindowUserPointer(w))->closeOnEscape
                  && !static_cast<Impl*>(glfwGetWindowUserPointer(w))->renderer->UIWantsKeyboard())
                    glfwSetWindowShouldClose(w, GLFW_TRUE);
            });
            renderer = std::make_unique<Renderer>(window.get(), config.renderer);
            glfwSetWindowUserPointer(window.get(), this);
            glfwSetMouseButtonCallback(window.get(), [](GLFWwindow* w, int button, int action, int) {
                auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
                if (button != GLFW_MOUSE_BUTTON_LEFT)
                    return;
                self->dragging = self->orbitEnabled && !self->renderer->UIWantsMouse() && action == GLFW_PRESS;
                if (self->dragging)
                    glfwGetCursorPos(w, &self->lastX, &self->lastY);
            });
            glfwSetCursorPosCallback(window.get(), [](GLFWwindow* w, double x, double y) {
                auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
                if (!self->orbitEnabled || !self->dragging)
                    return;
                const double dx = x - self->lastX, dy = y - self->lastY;
                self->lastX = x;
                self->lastY = y;
                self->UpdateCamera([&](OrbitCamera c) { return detail::DragOrbit(c, dx, dy); });
            });
            glfwSetScrollCallback(window.get(), [](GLFWwindow* w, double, double y) {
                auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
                if (self->orbitEnabled && !self->renderer->UIWantsMouse())
                    self->UpdateCamera([&](OrbitCamera c) { return detail::ZoomOrbit(c, -y * 100.0); });
            });
            glfwSetWindowFocusCallback(window.get(), [](GLFWwindow* w, int focused) {
                if (!focused)
                    static_cast<Impl*>(glfwGetWindowUserPointer(w))->dragging = false;
            });
#endif
        }
    };
    Window::Window(const WindowConfig& config)
    {
        ResolveGraphicsAPI(config.renderer);
        const auto max = static_cast<uint32_t>(std::numeric_limits<int>::max());
        if (!config.width || !config.height || config.width > max || config.height > max)
            throw std::invalid_argument("Window dimensions must be positive and fit in int");
        impl_ = std::make_unique<Impl>(config);
    }
    Window::~Window() = default;
    bool Window::PollEvents()
    {
        bool open = false;
#ifdef NOVA_HAS_SDL3
        if (impl_->sdlWindow)
            open = impl_->PollSDL();
        else
#endif
        {
#ifdef NOVA_HAS_GLFW
            glfwPollEvents();
            open = !glfwWindowShouldClose(impl_->window.get());
#endif
        }
        if (impl_->inputError)
        {
            const auto error  = impl_->inputError;
            impl_->inputError = nullptr;
            std::rethrow_exception(error);
        }
        return open;
    }
    void Window::CancelClose()
    {
#ifdef NOVA_HAS_SDL3
        if (impl_->sdlWindow)
        {
            impl_->sdlClose = false;
            return;
        }
#endif
#ifdef NOVA_HAS_GLFW
        glfwSetWindowShouldClose(impl_->window.get(), GLFW_FALSE);
#endif
    }
    void Window::SetSize(uint32_t width, uint32_t height)
    {
        constexpr auto max = static_cast<uint32_t>(std::numeric_limits<int>::max());
        if (!width || !height || width > max || height > max)
            throw std::invalid_argument("Window dimensions must be positive and fit in int");
#ifdef NOVA_HAS_SDL3
        if (impl_->sdlWindow)
        {
            if (!SDL_SetWindowSize(impl_->sdlWindow.get(), int(width), int(height)))
                throw std::runtime_error(SDL_GetError());
            return;
        }
#endif
#ifdef NOVA_HAS_GLFW
        glfwSetWindowSize(impl_->window.get(), int(width), int(height));
#endif
    }
    std::array<uint32_t, 2> Window::GetFramebufferSize() const
    {
        int width = 0, height = 0;
#ifdef NOVA_HAS_SDL3
        if (impl_->sdlWindow)
        {
            if (!(SDL_GetWindowFlags(impl_->sdlWindow.get()) & SDL_WINDOW_MINIMIZED))
                SDL_GetWindowSizeInPixels(impl_->sdlWindow.get(), &width, &height);
        }
        else
#endif
        {
#ifdef NOVA_HAS_GLFW
            glfwGetFramebufferSize(impl_->window.get(), &width, &height);
#endif
        }
        return {uint32_t(width), uint32_t(height)};
    }
    FrameResult Window::DrawFrame()
    {
        const auto [w, h] = GetFramebufferSize();
        impl_->renderer->Resize(w, h);
        const auto result = impl_->renderer->DrawFrame();
        if (result == FrameResult::Skipped)
        {
#ifdef NOVA_HAS_SDL3
            if (impl_->sdlWindow)
                SDL_Delay(10);
            else
#endif
            {
#ifdef NOVA_HAS_GLFW
                glfwWaitEventsTimeout(0.01);
#endif
            }
        }
        return result;
    }
    std::vector<AnimationClip> Window::GetAnimations() const
    {
        return impl_->renderer->GetAnimations();
    }
    AnimationState Window::GetAnimationState() const
    {
        return impl_->renderer->GetAnimationState();
    }
    void Window::SetAnimationState(const AnimationState& value)
    {
        impl_->renderer->SetAnimationState(value);
    }
    void Window::UpdateAnimation(float delta)
    {
        impl_->renderer->UpdateAnimation(delta);
    }
    std::vector<AnimationClip> Window::GetAnimations(const std::string& id) const
    {
        return impl_->renderer->GetAnimations(id);
    }
    AnimationState Window::GetAnimationState(const std::string& id) const
    {
        return impl_->renderer->GetAnimationState(id);
    }
    void Window::SetAnimationState(const std::string& id, const AnimationState& value)
    {
        impl_->renderer->SetAnimationState(id, value);
    }
    void Window::UpdateAnimations(float delta)
    {
        impl_->renderer->UpdateAnimations(delta);
    }
    AnimationCapabilities Window::GetAnimationCapabilities() const
    {
        return impl_->renderer->GetAnimationCapabilities();
    }
    std::optional<Bounds> Window::GetSceneInstanceBounds(const std::string& id) const
    {
        return impl_->renderer->GetSceneInstanceBounds(id);
    }


    std::optional<Bounds> Window::GetSceneAssetBounds(const std::string& id) const
    {
        return impl_->renderer->GetSceneAssetBounds(id);
    }
    std::optional<SceneHit> Window::PickScene(const Ray& ray) const
    {
        return impl_->renderer->PickScene(ray);
    }
    void Window::SetScene(const std::vector<SceneAsset>& assets, const std::vector<SceneInstance>& instances)
    {
        impl_->renderer->SetScene(assets, instances);
    }
    std::vector<Material> Window::GetMaterials(const std::string& id) const
    {
        return impl_->renderer->GetMaterials(id);
    }
    void Window::SetMaterial(uint32_t slot, const Material& value, const std::string& id)
    {
        impl_->renderer->SetMaterial(slot, value, id);
    }
    void Window::SetCloseOnEscape(bool enabled)
    {
        impl_->closeOnEscape = enabled;
    }
    void Window::LoadModel(const std::filesystem::path& path)
    {
        impl_->renderer->LoadModel(path);
    }
    OrbitCamera Window::GetCamera() const
    {
        return impl_->renderer->GetCamera();
    }
    void Window::EnableOrbitControls(bool enabled)
    {
        impl_->orbitEnabled = enabled;
        impl_->dragging     = false;
    }
    void Window::SetLights(const std::vector<SceneLight>& lights)
    {
        impl_->renderer->SetLights(lights);
    }
    std::vector<SceneLight> Window::GetLights() const
    {
        return impl_->renderer->GetLights();
    }
    LightingCapabilities Window::GetLightingCapabilities() const
    {
        return impl_->renderer->GetLightingCapabilities();
    }
    void Window::SetDirectionalLight(const DirectionalLight& light)
    {
        impl_->renderer->SetDirectionalLight(light);
    }
    DirectionalLight Window::GetDirectionalLight() const
    {
        return impl_->renderer->GetDirectionalLight();
    }
    bool Window::EnableParameterPanel(bool enabled)
    {
        return impl_->renderer->EnableParameterPanel(enabled);
    }
    void Window::SetClearColor(const std::array<float, 4>& color)
    {
        impl_->renderer->SetClearColor(color);
    }
    std::array<float, 4> Window::GetClearColor() const
    {
        return impl_->renderer->GetClearColor();
    }
    void Window::SetLighting(LightingMode mode)
    {
        impl_->renderer->SetLighting(mode);
    }
    RendererCapabilities Window::GetCapabilities() const
    {
        return impl_->renderer->GetCapabilities();
    }
    void Window::SetRenderMode(RenderMode mode)
    {
        impl_->renderer->SetRenderMode(mode);
    }
    RenderMode Window::GetRenderMode() const
    {
        return impl_->renderer->GetRenderMode();
    }
    void Window::SetPathTracing(const PathTracingSettings& value)
    {
        impl_->renderer->SetPathTracing(value);
    }
    PathTracingSettings Window::GetPathTracing() const
    {
        return impl_->renderer->GetPathTracing();
    }
    void Window::ResetAccumulation()
    {
        impl_->renderer->ResetAccumulation();
    }
    void Window::PresentImage(const Image& image)
    {
        impl_->renderer->PresentImage(image);
    }
    void Window::ClearImage()
    {
        impl_->renderer->ClearImage();
    }
    uint32_t Window::GetAccumulatedSamples() const
    {
        return impl_->renderer->GetAccumulatedSamples();
    }
    void Window::SetShadows(const SceneShadowSettings& settings)
    {
        impl_->renderer->SetShadows(settings);
    }
    SceneShadowSettings Window::GetShadows() const
    {
        return impl_->renderer->GetShadows();
    }
    void Window::SetFog(const FogSettings& settings)
    {
        impl_->renderer->SetFog(settings);
    }
    FogSettings Window::GetFog() const
    {
        return impl_->renderer->GetFog();
    }
    void Window::LoadEnvironment(const std::filesystem::path& path)
    {
        impl_->renderer->LoadEnvironment(path);
    }
    bool Window::EnableParameterPanel(const ParameterPanelConfig& config)
    {
        return impl_->renderer->EnableParameterPanel(config);
    }
    void Window::SetEnvironment(const EnvironmentSettings& settings)
    {
        impl_->renderer->SetEnvironment(settings);
    }
    EnvironmentSettings Window::GetEnvironment() const
    {
        return impl_->renderer->GetEnvironment();
    }
    void Window::SetViewCamera(const Camera& camera)
    {
        impl_->renderer->SetViewCamera(camera);
        impl_->orbitEnabled = false;
    }
    Camera Window::GetViewCamera() const
    {
        return impl_->renderer->GetViewCamera();
    }
    void Window::SetViewport(const std::optional<Viewport>& viewport)
    {
        impl_->renderer->SetViewport(viewport);
    }
    std::optional<Viewport> Window::GetViewport() const
    {
        return impl_->renderer->GetViewport();
    }
    void Window::SetCamera(const OrbitCamera& camera)
    {
        impl_->renderer->SetCamera(camera);
    }
    void Window::SetPostProcessing(const PostProcessingSettings& settings)
    {
        impl_->renderer->SetPostProcessing(settings);
    }
    PostProcessingSettings Window::GetPostProcessing() const
    {
        return impl_->renderer->GetPostProcessing();
    }
    MaterialCapabilities Window::GetMaterialCapabilities() const
    {
        return {};
    }
    void Window::SetRasterSettings(const RasterSettings& settings)
    {
        impl_->renderer->SetRasterSettings(settings);
    }
    RasterSettings Window::GetRasterSettings() const
    {
        return impl_->renderer->GetRasterSettings();
    }
    RasterCapabilities Window::GetRasterCapabilities() const
    {
        return impl_->renderer->GetRasterCapabilities();
    }
    void Window::SetAntiAliasing(const AntiAliasingSettings& settings)
    {
        impl_->renderer->SetAntiAliasing(settings);
    }
    AntiAliasingSettings Window::GetAntiAliasing() const
    {
        return impl_->renderer->GetAntiAliasing();
    }
    uint32_t Window::GetAntiAliasingHistoryFrames() const
    {
        return impl_->renderer->GetAntiAliasingHistoryFrames();
    }
    void Window::WaitIdle()
    {
        impl_->renderer->WaitIdle();
    }
    GraphicsAPI Window::GetAPI() const
    {
        return impl_->renderer->GetAPI();
    }
    uint32_t Window::ValidationErrorCount() const
    {
        return impl_->renderer->ValidationErrorCount();
    }
    void Window::SetHybrid(const HybridSettings& settings)
    {
        impl_->renderer->SetHybrid(settings);
    }
    HybridSettings Window::GetHybrid() const
    {
        return impl_->renderer->GetHybrid();
    }
    uint32_t Window::GetHybridHistoryFrames() const
    {
        return impl_->renderer->GetHybridHistoryFrames();
    }
    void Window::ResetHybridHistory()
    {
        impl_->renderer->ResetHybridHistory();
    }

    HybridCapabilities Window::GetHybridCapabilities() const
    {
        return impl_->renderer->GetHybridCapabilities();
    }
} // namespace render
