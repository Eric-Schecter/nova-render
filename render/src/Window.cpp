#include "render/Window.hpp"
#include "OrbitInput.hpp"
#include "PresentationWindow.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <exception>
#include <limits>
#include <stdexcept>
#include <vector>

namespace render
{
    namespace
    {
        bool windowContextActive = false; // Window's API is confined to the main thread.
        struct Context
        {
            Context()
            {
                if (windowContextActive)
                    throw std::runtime_error("Only one Nova Window is supported");
                if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
                    throw std::runtime_error(SDL_GetError());
                windowContextActive = true;
            }

            ~Context()
            {
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                windowContextActive = false;
            }
        };
    } // namespace
    struct Window::Impl
    {
        struct Finger
        {
            SDL_FingerID id;
            float        x, y;
        };

        Context                                                   context;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
        bool                                                      closeRequested = false;
        std::unique_ptr<Renderer>                                 renderer;
        std::optional<OrbitCamera>                                viewOrbit;
        bool                                                      orbitEnabled = false, dragging = false;
        SDL_TouchID                                               touchDevice = 0;
        std::vector<Finger>                                       fingers;
        bool                                                      closeOnEscape = true;
        std::exception_ptr                                        inputError;
        std::atomic_bool foreground{true};
        static bool TrackForeground(void* userdata, SDL_Event* event)
        {
            auto& self = *static_cast<Impl*>(userdata);
            if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND)
                self.foreground.store(false);
            if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
                self.foreground.store(true);
            return true;
        }
        ~Impl()
        {
            SDL_RemoveEventWatch(TrackForeground, this);
        }
        template <class Change>
        void UpdateCamera(Change change) noexcept
        {
            try
            {
                if (!viewOrbit)
                {
                    renderer->SetCamera(change(renderer->GetCamera()));
                    return;
                }
                const auto orbit = change(*viewOrbit);
                if (
                  orbit.yaw == viewOrbit->yaw && orbit.pitch == viewOrbit->pitch
                  && orbit.distance == viewOrbit->distance)
                    return;
                auto       view  = renderer->GetViewCamera();
                const auto pose  = CameraFromOrbit(orbit);
                view.position    = pose.position;
                view.orientation = pose.orientation;
                renderer->SetViewCamera(view);
                viewOrbit = orbit;
            }
            catch (...)
            {
                inputError = std::current_exception();
            }
        }
        void HandleTouch(const SDL_TouchFingerEvent& event)
        {
            if (!orbitEnabled || event.touchID == SDL_MOUSE_TOUCHID)
                return;
            if (!fingers.empty() && event.touchID != touchDevice)
                return;
            auto finger = std::find_if(fingers.begin(), fingers.end(), [&](const Finger& active) {
                return active.id == event.fingerID;
            });
            if (event.type == SDL_EVENT_FINGER_DOWN)
            {
                if (finger == fingers.end() && fingers.size() < 2 && !renderer->UIWantsMouse())
                {
                    touchDevice = event.touchID;
                    fingers.push_back({event.fingerID, event.x, event.y});
                }
                return;
            }
            if (finger == fingers.end())
                return;
            if (event.type != SDL_EVENT_FINGER_MOTION)
            {
                fingers.erase(finger);
                return;
            }
            const auto previous = *finger;
            finger->x           = event.x;
            finger->y           = event.y;
            if (fingers.size() == 1)
            {
                UpdateCamera([&](OrbitCamera c) {
                    return detail::DragOrbit(c, (event.x - previous.x) * 600.0, (event.y - previous.y) * 450.0);
                });
                return;
            }
            int width = 0, height = 0;
            if (!SDL_GetWindowSize(window.get(), &width, &height))
                return;
            const auto& other = finger == fingers.begin() ? fingers.back() : fingers.front();
            const auto span = [&](const Finger& point) {
                return std::hypot(double(point.x - other.x) * width, double(point.y - other.y) * height);
            };
            const auto before = span(previous), after = span(*finger);
            if (before > 1.0 && after > 1.0)
                UpdateCamera([&](OrbitCamera c) { return detail::ZoomOrbit(c, 1000.0 * std::log(before / after)); });
        }
        bool PollSDL()
        {
            SDL_Event  event;
            const auto id = SDL_GetWindowID(window.get());
            while (SDL_PollEvent(&event))
            {
                renderer->ProcessWindowEvent(&event);
                switch (event.type)
                {
                case SDL_EVENT_QUIT: closeRequested = true; break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (event.window.windowID == id)
                        closeRequested = true;
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    if (event.window.windowID == id)
                    {
                        dragging = false;
                        fingers.clear();
                    }
                    break;
                case SDL_EVENT_WILL_ENTER_BACKGROUND: fingers.clear(); break;
                case SDL_EVENT_KEY_DOWN:
                    if (
                      event.key.windowID == id && event.key.key == SDLK_ESCAPE && !event.key.repeat && closeOnEscape
                      && !renderer->UIWantsKeyboard())
                        closeRequested = true;
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (
                      event.button.windowID == id && event.button.which != SDL_TOUCH_MOUSEID
                      && event.button.button == SDL_BUTTON_LEFT)
                        dragging =
                          event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && orbitEnabled && !renderer->UIWantsMouse();
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (event.motion.windowID == id && event.motion.which != SDL_TOUCH_MOUSEID && dragging && orbitEnabled)
                        UpdateCamera(
                          [&](OrbitCamera c) { return detail::DragOrbit(c, event.motion.xrel, event.motion.yrel); });
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    if (event.wheel.windowID == id && orbitEnabled && !renderer->UIWantsMouse())
                    {
                        // SDL's delta already follows the system scroll direction.
                        UpdateCamera([&](OrbitCamera c) { return detail::ZoomOrbit(c, -event.wheel.y * 100.0); });
                    }
                    break;
                case SDL_EVENT_FINGER_DOWN:
                case SDL_EVENT_FINGER_UP:
                case SDL_EVENT_FINGER_MOTION:
                case SDL_EVENT_FINGER_CANCELED:
                    if (event.tfinger.windowID == id)
                        HandleTouch(event.tfinger);
                    break;
                }
            }
            return !closeRequested;
        }
        explicit Impl(const WindowConfig& config)
        {
            const bool vulkan = ResolveGraphicsAPI(config.renderer) == GraphicsAPI::Vulkan;
            window.reset(
              detail::CreateSDLWindow(config.title.c_str(), int(config.width), int(config.height), vulkan, true));
            if (!window)
                throw std::runtime_error(SDL_GetError());
            renderer.reset(new Renderer(detail::PresentationWindow(window.get()), config.renderer));
            if (!SDL_AddEventWatch(TrackForeground, this))
                throw std::runtime_error(SDL_GetError());
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
        const bool open = impl_->PollSDL();
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
        impl_->closeRequested = false;
    }
    void Window::SetSize(uint32_t width, uint32_t height)
    {
        constexpr auto max = static_cast<uint32_t>(std::numeric_limits<int>::max());
        if (!width || !height || width > max || height > max)
            throw std::invalid_argument("Window dimensions must be positive and fit in int");
        if (!SDL_SetWindowSize(impl_->window.get(), int(width), int(height)))
            throw std::runtime_error(SDL_GetError());
    }
    std::array<uint32_t, 2> Window::GetFramebufferSize() const
    {
        if (!impl_->foreground.load())
            return {};
        int width = 0, height = 0;
        if (!(SDL_GetWindowFlags(impl_->window.get()) & SDL_WINDOW_MINIMIZED))
            SDL_GetWindowSizeInPixels(impl_->window.get(), &width, &height);
        return {uint32_t(width), uint32_t(height)};
    }
    FrameResult Window::DrawFrame()
    {
        const auto [w, h] = GetFramebufferSize();
        impl_->renderer->Resize(w, h);
        const auto result = impl_->renderer->DrawFrame();
        if (result == FrameResult::Skipped)
            SDL_Delay(10);
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
    void Window::LoadModel(const std::filesystem::path& path, uint32_t materialVariant)
    {
        impl_->renderer->LoadModel(path, materialVariant);
    }
    OrbitCamera Window::GetCamera() const
    {
        return impl_->renderer->GetCamera();
    }
    void Window::EnableOrbitControls(bool enabled)
    {
        impl_->viewOrbit.reset();
        impl_->fingers.clear();
        impl_->orbitEnabled = enabled;
        impl_->dragging     = false;
    }
    void Window::EnableViewOrbitControls(float distance)
    {
        const auto view         = GetViewCamera();
        const auto [x, y, z, w] = view.orientation;
        const std::array<float, 3> backward{2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
        constexpr float            degrees = 57.29577951308232f;
        OrbitCamera                orbit;
        orbit.yaw      = std::atan2(backward[0], backward[2]) * degrees;
        orbit.pitch    = std::clamp(std::asin(std::clamp(backward[1], -1.f, 1.f)) * degrees, -85.f, 85.f);
        orbit.distance = distance;
        orbit.exposure = view.exposure;
        for (size_t i = 0; i < orbit.target.size(); ++i)
            orbit.target[i] = view.position[i] - backward[i] * distance;
        (void)CameraFromOrbit(orbit);
        EnableOrbitControls();
        impl_->viewOrbit = orbit;
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
        EnableOrbitControls(false);
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
        impl_->viewOrbit.reset();
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
