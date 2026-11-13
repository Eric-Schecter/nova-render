#include "render/Window.hpp"
#include "OrbitInput.hpp"
#include <GLFW/glfw3.h>
#include <exception>
#include <limits>
#include <stdexcept>

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
                if (!glfwInit())
                    throw std::runtime_error("GLFW initialization failed");
                windowContextActive = true;
            }
            ~Context()
            {
                glfwTerminate();
                windowContextActive = false;
            }
        };
    } // namespace
    struct Window::Impl
    {
        Context                                                   context;
        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window{nullptr, glfwDestroyWindow};
        std::unique_ptr<Renderer>                                 renderer;
        bool                                                      orbitEnabled = false, dragging = false;
        double                                                    lastX = 0, lastY = 0;
        std::exception_ptr                                        inputError;
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
            if (window)
            {
                glfwSetMouseButtonCallback(window.get(), nullptr);
                glfwSetCursorPosCallback(window.get(), nullptr);
                glfwSetScrollCallback(window.get(), nullptr);
                glfwSetWindowFocusCallback(window.get(), nullptr);
                glfwSetWindowUserPointer(window.get(), nullptr);
            }
        }
        explicit Impl(const WindowConfig& config)
        {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            window.reset(glfwCreateWindow(
              static_cast<int>(config.width), static_cast<int>(config.height), config.title.c_str(), nullptr, nullptr));
            if (!window)
                throw std::runtime_error("Window creation failed");
            glfwSetKeyCallback(window.get(), [](GLFWwindow* w, int key, int, int action, int) {
                if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
                    glfwSetWindowShouldClose(w, GLFW_TRUE);
            });
            renderer = std::make_unique<Renderer>(window.get(), config.renderer);
            glfwSetWindowUserPointer(window.get(), this);
            glfwSetMouseButtonCallback(window.get(), [](GLFWwindow* w, int button, int action, int) {
                auto* self = static_cast<Impl*>(glfwGetWindowUserPointer(w));
                if (button != GLFW_MOUSE_BUTTON_LEFT)
                    return;
                self->dragging = self->orbitEnabled && action == GLFW_PRESS;
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
                if (self->orbitEnabled)
                    self->UpdateCamera([&](OrbitCamera c) { return detail::ZoomOrbit(c, -y * 100.0); });
            });
            glfwSetWindowFocusCallback(window.get(), [](GLFWwindow* w, int focused) {
                if (!focused)
                    static_cast<Impl*>(glfwGetWindowUserPointer(w))->dragging = false;
            });
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
        glfwPollEvents();
        if (impl_->inputError)
        {
            const auto error  = impl_->inputError;
            impl_->inputError = nullptr;
            std::rethrow_exception(error);
        }
        return !glfwWindowShouldClose(impl_->window.get());
    }
    FrameResult Window::DrawFrame()
    {
        int w, h;
        glfwGetFramebufferSize(impl_->window.get(), &w, &h);
        impl_->renderer->Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        const auto result = impl_->renderer->DrawFrame();
        if (result == FrameResult::Skipped)
            glfwWaitEventsTimeout(0.01);
        return result;
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
    void Window::SetDirectionalLight(const DirectionalLight& light)
    {
        impl_->renderer->SetDirectionalLight(light);
    }
    DirectionalLight Window::GetDirectionalLight() const
    {
        return impl_->renderer->GetDirectionalLight();
    }
    void Window::SetLighting(LightingMode mode)
    {
        impl_->renderer->SetLighting(mode);
    }
    void Window::SetCamera(const OrbitCamera& camera)
    {
        impl_->renderer->SetCamera(camera);
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
} // namespace render
