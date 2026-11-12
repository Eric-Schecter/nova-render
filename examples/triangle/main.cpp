#include "render/Renderer.hpp"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

struct GlfwContext {
    GlfwContext() {
        glfwSetErrorCallback([](int code, const char* message) {
            std::cerr << "GLFW " << code << ": " << message << '\n';
        });
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
    }
    ~GlfwContext() { glfwTerminate(); }
};

static void waitForIconified(GLFWwindow* window, bool expected) {
    const double deadline = glfwGetTime() + 5.0;
    while ((glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE) != expected) {
        if (glfwWindowShouldClose(window) || glfwGetTime() >= deadline)
            throw std::runtime_error(expected ? "Window did not minimize" : "Window did not restore");
        glfwWaitEventsTimeout(0.01);
    }
}

int main(int argc, char** argv) {
    try {
        render::GraphicsAPI api = render::GraphicsAPI::Auto;
        std::string shaderDirectory = TRIANGLE_SHADER_DIR;
        int frameLimit = 0;
        bool resizeTest = false, warp = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--frames" && i + 1 < argc) {
                const std::string value = argv[++i];
                size_t consumed = 0;
                frameLimit = std::stoi(value, &consumed);
                if (frameLimit <= 0 || consumed != value.size())
                    throw std::runtime_error("--frames must be a positive integer");
            } else if (arg == "--api" && i + 1 < argc) {
                const std::string name = argv[++i];
                if (name == "vulkan") api = render::GraphicsAPI::Vulkan;
                else if (name == "metal") api = render::GraphicsAPI::Metal;
                else if (name == "dx12") api = render::GraphicsAPI::D3D12;
                else if (name == "auto") api = render::GraphicsAPI::Auto;
                else throw std::runtime_error("Unknown API: " + name);
            } else if (arg == "--shader-dir" && i + 1 < argc) shaderDirectory = argv[++i];
            else if (arg == "--warp") warp = true;
            else if (arg == "--resize-test") resizeTest = true;
            else throw std::runtime_error("Usage: nova_triangle [--api auto|vulkan|metal|dx12] [--warp] [--shader-dir PATH] [--frames N] [--resize-test]");
        }
        render::RendererConfig config;
        config.api = api;
        config.useWarp = warp;
        config.shaderDirectory = shaderDirectory;
#ifdef NDEBUG
        config.enableValidation = false;
#endif
        render::ResolveGraphicsAPI(config); // Reject unsupported requests before creating a window.
        GlfwContext glfw;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
            glfwCreateWindow(800, 600, "Nova | Triangle", nullptr, nullptr), glfwDestroyWindow);
        if (!window) throw std::runtime_error("Window creation failed");
        glfwSetKeyCallback(window.get(), [](GLFWwindow* w, int key, int, int action, int) {
            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(w, GLFW_TRUE);
        });
        render::Renderer renderer(window.get(), config);
        const std::string title = std::string("Nova | ") + render::GraphicsAPIName(renderer.GetAPI()) + " Triangle" + (warp ? " (WARP)" : "");
        glfwSetWindowTitle(window.get(), title.c_str());
        glfwSetWindowUserPointer(window.get(), &renderer);
        glfwSetFramebufferSizeCallback(window.get(), [](GLFWwindow* w, int width, int height) {
            static_cast<render::Renderer*>(glfwGetWindowUserPointer(w))->Resize(width, height);
        });
        int frames = 0;
        bool firstResize = false, secondResize = false, suspendTest = false;
        while (!glfwWindowShouldClose(window.get())) {
            glfwPollEvents();
            if (glfwWindowShouldClose(window.get())) break;
            if (resizeTest && frames >= 20 && !firstResize) {
                glfwSetWindowSize(window.get(), 1024, 640); firstResize = true;
            }
            if (resizeTest && frames >= 40 && !secondResize) {
                glfwSetWindowSize(window.get(), 800, 600); secondResize = true;
            }
            if (resizeTest && frames >= 60 && !suspendTest) {
                glfwIconifyWindow(window.get());
                waitForIconified(window.get(), true);
                renderer.Resize(0, 0);
                if (renderer.DrawFrame() != render::FrameResult::Skipped)
                    throw std::runtime_error("Zero-size frame was not skipped");
                glfwRestoreWindow(window.get());
                waitForIconified(window.get(), false);
                glfwPollEvents();
                suspendTest = true;
                std::cout << "Suspend/resume check passed\n";
            }
            int width, height;
            glfwGetFramebufferSize(window.get(), &width, &height);
            if (glfwGetWindowAttrib(window.get(), GLFW_ICONIFIED)) width = height = 0;
            renderer.Resize(width, height);
            if (renderer.DrawFrame() == render::FrameResult::Rendered) ++frames;
            else glfwWaitEventsTimeout(0.05);
            if (frameLimit && frames >= frameLimit) break;
        }
        renderer.WaitIdle();
        std::cout << "Rendered " << frames << " frames; validation errors: "
                  << renderer.ValidationErrorCount() << '\n';
        return renderer.ValidationErrorCount() ? EXIT_FAILURE : EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
