#include "Backend.hpp"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    GLFWwindow* window = nullptr;
    try
    {
        if (argc < 3)
            throw std::invalid_argument("Expected API and assets directory");
        const std::string           api    = argv[1];
        const std::filesystem::path assets = argv[2];
        render::RendererConfig      config;
        config.api     = api == "metal"    ? render::GraphicsAPI::Metal
                         : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                           : render::GraphicsAPI::D3D12;
        config.useWarp = api == "dx12";
        if (!glfwInit())
            throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(320, 240, "Scene API validation", nullptr, nullptr);
        if (!window)
            throw std::runtime_error("Window creation failed");
        std::unique_ptr<render::detail::Backend> backend;
#ifdef __APPLE__
        if (api == "metal")
            backend = render::detail::CreateMetalBackend(window, config);
#endif
#ifdef NOVA_TEST_VULKAN
        if (api == "vulkan")
            backend = render::detail::CreateVulkanBackend(window, config);
#endif
#ifdef _WIN32
        if (api == "dx12")
            backend = render::detail::CreateD3D12Backend(window, config);
#endif
        if (!backend)
            throw std::runtime_error("Backend unavailable");
        backend->ConfigurePathTracing(config.api, config);
        std::vector<render::SceneAsset> models{
          {"red", assets / "SceneSample/Red.glb"}, {"blue", assets / "SceneSample/Blue.glb"}};
        std::vector<render::SceneInstance> instances{{"upper", "red"}, {"lower", "red"}, {"right", "blue"}};
        for (size_t i = 0; i < instances.size(); ++i)
        {
            auto& world = instances[i].world;
            world[0] = world[5] = world[10] = .55f;
            world[12]                       = i == 2 ? 1.2f : -1.2f;
            world[13]                       = i == 0 ? .8f : i == 1 ? -.8f : 0;
        }
        backend->SetScene(models, instances);
        auto camera     = backend->GetCamera();
        camera.distance = 7;
        backend->SetCamera(camera);
        backend->SetClearColor({0, 0, 0, 1});
        auto draw = [&] {
            glfwPollEvents();
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            backend->Resize(w, h);
            if (backend->DrawFrame() != render::FrameResult::Rendered)
                throw std::runtime_error("Skipped scene frame");
            backend->WaitIdle();
            if (backend->ValidationErrorCount())
                throw std::runtime_error("GPU validation errors");
        };
        draw(); // Off uses the direct raster pass.
        auto inspect = [&](bool expectEmpty = false) {
            const auto& image    = backend->GetRasterAAImage();
            size_t      redUpper = 0, redLower = 0, blue = 0;
            for (uint32_t y = 0; y < image.height; ++y)
                for (uint32_t x = 0; x < image.width; ++x)
                {
                    auto p = &image.pixels[(size_t(y) * image.width + x) * 4];
                    if (p[0] > 150 && p[0] > p[2] * 2 && x < image.width / 2)
                        (y < image.height / 2 ? redUpper : redLower)++;
                    if (p[2] > 150 && p[2] > p[0] * 2 && x > image.width / 2)
                        blue++;
                }
            if (expectEmpty ? redUpper + redLower + blue != 0 : redUpper < 50 || redLower < 50 || blue < 50)
                throw std::runtime_error("Readback missed independent colored instances");
            return image;
        };
        for (unsigned method = 1; method <= 5; ++method)
        {
            render::AntiAliasingSettings aa;
            aa.method = render::AAMethod(method);
            backend->SetAntiAliasing(aa);
            draw();
            inspect();
        }
        const auto normalized = inspect();
        for (auto& model : models)
            model.sourceSpace = true;
        for (auto& i : instances)
        {
            i.world[0] = i.world[5] = i.world[10] = .275f;
            i.world[12] -= .55f;
            i.world[13] -= .275f;
        }
        backend->SetScene(models, instances);
        draw();
        const auto source = inspect();
        if (source.pixels != normalized.pixels)
            throw std::runtime_error("Source-space equivalent transforms changed rendering");
        if (backend->GetCapabilities().rayTracing)
            throw std::runtime_error("Scene incorrectly advertises single-model ray tracing");
        backend->SetScene({}, {});
        draw();
        inspect(true);
        backend->LoadModel(render::detail::LoadGLB(assets / "SceneSample/Red.glb"));
        draw();
        if (backend->scene.active)
            throw std::runtime_error("LoadModel did not exit scene mode");
        backend.reset();
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        std::cout << "PASS " << api
                  << " scene render: grouped draws, all AA methods, source equivalence, clear and legacy model\n";
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
