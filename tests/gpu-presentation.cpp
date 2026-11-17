#include "Backend.hpp"
#include "TestDevice.hpp"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <stb_image_write.h>
using namespace render;
static double Difference(const Image& a, const Image& b)
{
    if (a.pixels.size() != b.pixels.size())
        throw std::runtime_error("Size mismatch");
    double d = 0;
    for (size_t i = 0; i < a.pixels.size(); i++)
        d += std::abs(int(a.pixels[i]) - int(b.pixels[i]));
    return d / a.pixels.size();
}
int main(int argc, char** argv)
{
    GLFWwindow* window = nullptr;
    std::cout.setf(std::ios::unitbuf);
    try
    {
        const std::string           api    = argc > 1 ? TestAPI(argv[1]) : "metal";
        const std::filesystem::path assets = argc > 2 ? argv[2] : "assets";
        RendererConfig              config;
        config.api = api == "metal" ? GraphicsAPI::Metal : api == "vulkan" ? GraphicsAPI::Vulkan : GraphicsAPI::D3D12;
        config.useWarp = argc > 1 && TestUseWarp(argv[1]);
        if (!glfwInit())
            throw std::runtime_error("GLFW init");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(160, 120, "GPU presentation validation", nullptr, nullptr);
        if (!window)
            throw std::runtime_error("Window init");
        std::unique_ptr<detail::Backend> backend;
#ifdef __APPLE__
        if (api == "metal")
            backend = detail::CreateMetalBackend(window, config);
#endif
#ifdef NOVA_TEST_VULKAN
        if (api == "vulkan")
            backend = detail::CreateVulkanBackend(window, config);
#endif
#ifdef _WIN32
        if (api == "dx12")
            backend = detail::CreateD3D12Backend(window, config);
#endif
        if (!backend)
            throw std::runtime_error("Backend unavailable");
        backend->ConfigurePathTracing(config.api, config);
        PostProcessingSettings post;
        post.enabled      = true;
        post.bloom        = false;
        post.autoExposure = false;
        post.toneMapping  = ToneMapping::ACES;
        backend->SetPostProcessing(post);
        auto dir = std::filesystem::path("build/validation/gpu-presentation") / api;
        std::filesystem::create_directories(dir);
        auto draw = [&](const std::string& name) {
            glfwPollEvents();
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            backend->Resize(w, h);
            if (backend->DrawFrame() != FrameResult::Rendered)
                throw std::runtime_error("Skipped frame");
            backend->WaitIdle();
            if (backend->ValidationErrorCount())
                throw std::runtime_error("GPU validation errors");
            auto image = backend->GetRasterAAImage();
            if (image.pixels.empty())
                throw std::runtime_error("Empty image");
            stbi_write_png(
              (dir / (name + ".png")).string().c_str(),
              image.width,
              image.height,
              4,
              image.pixels.data(),
              image.width * 4);
            return image;
        };

        backend->LoadModel(detail::LoadGLB(assets / "DamagedHelmet/DamagedHelmet.glb"));
        for (auto pipeline : {RasterPipeline::Forward, RasterPipeline::Deferred})
        {
            RasterSettings raster;
            raster.pipeline = pipeline;
            backend->SetRasterSettings(raster);
            backend->EnableGPUFramePresentation(false);
            auto       expected  = draw("cpu-" + std::to_string(int(pipeline)));
            const auto readbacks = backend->RasterReadbackCount();
            backend->EnableGPUFramePresentation(true);
            for (int i = 0; i < 3; i++)
            {
                glfwPollEvents();
                int w, h;
                glfwGetFramebufferSize(window, &w, &h);
                backend->Resize(w, h);
                if (backend->DrawFrame() != FrameResult::Rendered)
                    throw std::runtime_error("GPU frame skipped");
                backend->WaitIdle();
                if (
                  !backend->RasterGPUOutput() || !backend->GetRasterAAImage().pixels.empty()
                  || backend->RasterReadbackCount() != readbacks)
                    throw std::runtime_error("GPU frame used CPU readback");
            }
            auto actual = backend->CaptureRasterFrame();
            if (Difference(expected, actual) > .05)
                throw std::runtime_error("GPU output mismatch");
            stbi_write_png(
              (dir / ("gpu-" + std::to_string(int(pipeline)) + ".png")).string().c_str(),
              actual.width,
              actual.height,
              4,
              actual.pixels.data(),
              actual.width * 4);
            if (backend->ValidationErrorCount())
                throw std::runtime_error("GPU validation errors");
        }
        backend.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout << api << " GPU presentation: zero frame readbacks, explicit capture parity passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
