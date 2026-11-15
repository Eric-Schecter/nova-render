#include "Backend.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv)
{
    GLFWwindow* window = nullptr;
    try
    {
        render::RendererConfig config;
        std::string            api = argc > 1 ? argv[1] : "metal";
        config.api                 = api == "metal"    ? render::GraphicsAPI::Metal
                                     : api == "vulkan" ? render::GraphicsAPI::Vulkan
                                                       : render::GraphicsAPI::D3D12;
        config.useWarp             = api == "dx12";
        if (!glfwInit())
            throw std::runtime_error("GLFW init");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(240, 180, "Raster AA validation", nullptr, nullptr);
        if (!window)
            throw std::runtime_error("Window creation");
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
        auto draw = [&] {
            glfwPollEvents();
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            backend->Resize(w, h);
            if (backend->DrawFrame() != render::FrameResult::Rendered)
                throw std::runtime_error("Skipped AA frame");
            backend->WaitIdle();
            if (backend->ValidationErrorCount())
                throw std::runtime_error("GPU validation errors");
        };
        if (argc < 3)
            throw std::runtime_error("Expected API and assets directory");
        const std::filesystem::path assets = argv[2];
        for (unsigned method = 1; method < 6; method++)
        {
            render::AntiAliasingSettings aa;
            aa.method = render::AAMethod(method);
            backend->SetAntiAliasing(aa);
            draw();
        }
        {
            render::AntiAliasingSettings aa;
            backend->SetAntiAliasing(aa);
            draw();
        }

        backend->LoadModel(render::detail::LoadGLB(assets / "DamagedHelmet/DamagedHelmet.glb"));
        for (unsigned method = 0; method < 6; method++)
        {
            render::AntiAliasingSettings settings;
            settings.method = render::AAMethod(method);
            backend->SetAntiAliasing(settings);
            draw();
            if (method)
            {
                const auto& image = backend->GetRasterAAImage();
                if (image.pixels.empty())
                    throw std::runtime_error("No raster AA output");
                unsigned lo = 255, hi = 0;
                for (size_t i = 0; i < image.pixels.size(); i += 4)
                {
                    lo = std::min(lo, unsigned(image.pixels[i]));
                    hi = std::max(hi, unsigned(image.pixels[i]));
                }
                if (hi - lo < 20)
                    throw std::runtime_error("Blank AA image");
            }
            if (method == 5)
            {
                draw();
                if (backend->GetAntiAliasingHistoryFrames() != 2)
                    throw std::runtime_error("TAA did not accumulate");
                auto camera = backend->GetCamera();
                camera.yaw += 3;
                backend->SetCamera(camera);
                draw();
                if (backend->GetAntiAliasingHistoryFrames() != 3)
                    throw std::runtime_error("Small camera motion lost history");
                camera.yaw += 40;
                backend->SetCamera(camera);
                if (backend->GetAntiAliasingHistoryFrames() != 0)
                    throw std::runtime_error("Camera cut retained history");
            }
            std::cout << "AA method " << method << " passed\n";
        }
        backend->LoadEnvironment(render::detail::LoadEnvironmentFile(assets / "Environment/venice_sunset.nenv"));
        backend->SetLighting(render::LightingMode::PBR);
        for (unsigned method = 1; method < 6; method++)
        {
            render::AntiAliasingSettings aa;
            aa.method = render::AAMethod(method);
            backend->SetAntiAliasing(aa);
            draw();
        }
        backend->LoadModel(render::detail::LoadGLB(assets / "Fox/Fox.glb"));
        render::AntiAliasingSettings settings;
        settings.method = render::AAMethod::TAA;
        backend->SetAntiAliasing(settings);
        draw();
        auto state = backend->animation.state;
        state.time = .1f;
        backend->SetAnimationState(state);
        draw();
        if (backend->GetAntiAliasingHistoryFrames() != 2)
            throw std::runtime_error("Skinning TAA history failed");
        render::PostProcessingSettings post;
        post.enabled      = true;
        post.autoExposure = true;
        backend->SetPostProcessing(post);
        for (unsigned method = 0; method < 6; method++)
        {
            settings.method = render::AAMethod(method);
            backend->SetAntiAliasing(settings);
            draw();
        }
        post.enabled = false;
        backend->SetPostProcessing(post);
        draw();
        backend.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout << "PASS model raster AA, camera/cuts, skinning and validation\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
