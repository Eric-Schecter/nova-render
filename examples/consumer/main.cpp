#include <render/Renderer.hpp>
#include <GLFW/glfw3.h>
#include <iostream>
#include <memory>
#include <stdexcept>
struct GlfwContext {
    GlfwContext() { if (!glfwInit()) throw std::runtime_error("GLFW initialization failed"); }
    ~GlfwContext() { glfwTerminate(); }
};
int main(int argc, char** argv) {
    try {
        GlfwContext glfw;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(800, 600, "Nova installed consumer", nullptr, nullptr), glfwDestroyWindow);
        if (!window) throw std::runtime_error("Window creation failed");
        render::RendererConfig config; // Uses shaders embedded in Nova::Render.
        if (argc > 1) {
            const std::string api = argv[1];
            if (api == "vulkan") config.api = render::GraphicsAPI::Vulkan;
            else if (api == "metal") config.api = render::GraphicsAPI::Metal;
            else if (api == "dx12") config.api = render::GraphicsAPI::D3D12;
            else throw std::runtime_error("Usage: nova_consumer [vulkan|metal|dx12]");
        }
        render::Renderer renderer(window.get(), config);
        int frames = 0;
        while (!glfwWindowShouldClose(window.get()) && frames < 120) {
            glfwPollEvents();
            int w, h; glfwGetFramebufferSize(window.get(), &w, &h);
            renderer.Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            if (renderer.DrawFrame() == render::FrameResult::Rendered) ++frames;
            else glfwWaitEventsTimeout(0.01);
        }
        renderer.WaitIdle();
        std::cout << "Rendered " << frames << " frames; validation errors: " << renderer.ValidationErrorCount() << '\n';
        return frames == 120 && renderer.ValidationErrorCount() == 0 ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
