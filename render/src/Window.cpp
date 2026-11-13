#include "render/Window.hpp"
#include <GLFW/glfw3.h>
#include <limits>
#include <stdexcept>

namespace render {
namespace {
bool windowContextActive = false; // Window's API is confined to the main thread.
struct Context {
    Context() {
        if (windowContextActive) throw std::runtime_error("Only one Nova Window is supported");
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        windowContextActive = true;
    }
    ~Context() { glfwTerminate(); windowContextActive = false; }
};
}
struct Window::Impl {
    Context context;
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window{nullptr, glfwDestroyWindow};
    std::unique_ptr<Renderer> renderer;
    explicit Impl(const WindowConfig& config) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window.reset(glfwCreateWindow(static_cast<int>(config.width), static_cast<int>(config.height),
            config.title.c_str(), nullptr, nullptr));
        if (!window) throw std::runtime_error("Window creation failed");
        glfwSetKeyCallback(window.get(), [](GLFWwindow* w, int key, int, int action, int) {
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(w, GLFW_TRUE);
        });
        renderer = std::make_unique<Renderer>(window.get(), config.renderer);
    }
};
Window::Window(const WindowConfig& config) {
    ResolveGraphicsAPI(config.renderer);
    const auto max = static_cast<uint32_t>(std::numeric_limits<int>::max());
    if (!config.width || !config.height || config.width > max || config.height > max)
        throw std::invalid_argument("Window dimensions must be positive and fit in int");
    impl_ = std::make_unique<Impl>(config);
}
Window::~Window() = default;
bool Window::PollEvents() { glfwPollEvents(); return !glfwWindowShouldClose(impl_->window.get()); }
FrameResult Window::DrawFrame() {
    int w, h;
    glfwGetFramebufferSize(impl_->window.get(), &w, &h);
    impl_->renderer->Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    const auto result = impl_->renderer->DrawFrame();
    if (result == FrameResult::Skipped) glfwWaitEventsTimeout(0.01);
    return result;
}
void Window::WaitIdle() { impl_->renderer->WaitIdle(); }
GraphicsAPI Window::GetAPI() const { return impl_->renderer->GetAPI(); }
uint32_t Window::ValidationErrorCount() const { return impl_->renderer->ValidationErrorCount(); }
}
