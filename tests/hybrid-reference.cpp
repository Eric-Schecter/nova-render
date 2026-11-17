#include "Backend.hpp"
#include "TestDevice.hpp"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
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
        window = glfwCreateWindow(160, 90, "GPU presentation validation", nullptr, nullptr);
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
        auto dir = std::filesystem::path("build/validation/hybrid-reference") / api;
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


        std::ifstream    source(argc > 2 ? argv[2] : "build/reference/hybrid-rendering/scenes.json");
        auto             fixture = nlohmann::json::parse(source);
        DirectionalLight light;
        light.direction = fixture["light"]["direction"].get<std::array<float, 3>>();
        light.color     = {1, 1, 1};
        light.intensity = 4;
        backend->SetDirectionalLight(light);
        backend->SetLighting(LightingMode::PBR);
        SceneShadowSettings shadows;
        shadows.enabled  = true;
        shadows.softness = 0;
        shadows.samples  = 1;
        backend->SetShadows(shadows);
        HybridSettings hybrid;
        hybrid.shadowSamples = 1;
        backend->SetHybrid(hybrid);
        RasterSettings raster;
        raster.pipeline = RasterPipeline::Deferred;
        backend->SetRasterSettings(raster);
        size_t captures = 0;
        double maximum  = 0;
        for (const auto& scene : fixture["scenes"])
        {
            backend->SetRenderMode(RenderMode::Raster);
            std::vector<SceneAsset>    assets;
            std::vector<SceneInstance> instances;
            for (const auto& a : scene["assets"])
                assets.push_back({a["id"].get<std::string>(), a["path"].get<std::string>(), true});
            for (const auto& i : scene["instances"])
                instances.push_back(
                  {i["id"].get<std::string>(), i["assetId"].get<std::string>(), i["world"].get<Matrix4>()});
            backend->SetScene(assets, instances);
            size_t triangles = 0;
            for (const auto& d : backend->scene.draws)
                triangles += size_t(d.vertexCount / 3) * d.instanceCount;
            if (triangles != scene["triangles"].get<size_t>())
                throw std::runtime_error("Reference triangle count mismatch");
            for (const auto& c : scene["cameras"])
            {
                Camera camera;
                camera.position    = c["position"].get<std::array<float, 3>>();
                camera.orientation = c["orientation"].get<std::array<float, 4>>();
                camera.verticalFov = 60;
                camera.nearPlane   = 1;
                camera.farPlane    = 1000;
                backend->SetViewCamera(camera);
                std::string name = scene["id"].get<std::string>() + "-" + c["id"].get<std::string>();
                backend->SetRenderMode(RenderMode::Raster);
                hybrid.view = HybridView::Final;
                backend->SetHybrid(hybrid);
                auto baseline = draw(name + "-raster");
                backend->SetRenderMode(RenderMode::Hybrid);
                glfwPollEvents();
                int w, h;
                glfwGetFramebufferSize(window, &w, &h);
                backend->Resize(w, h);
                auto before = backend->RasterReadbackCount();
                backend->DrawFrame();
                backend->WaitIdle();
                if (backend->RasterReadbackCount() != before)
                    throw std::runtime_error("Reference Hybrid readback");
                auto image = backend->CaptureRasterFrame();
                stbi_write_png(
                  (dir / (name + "-hybrid.png")).string().c_str(),
                  image.width,
                  image.height,
                  4,
                  image.pixels.data(),
                  image.width * 4);
                auto difference = Difference(image, baseline);
                maximum         = std::max(maximum, difference);
                if (difference > 1)
                    throw std::runtime_error(
                      "Reference direct-light parity " + name + ": " + std::to_string(difference));
                if (backend->ValidationErrorCount())
                    throw std::runtime_error("Reference GPU validation error");
                captures += 2;
                std::cout << name << ": " << triangles << " triangles, MAE " << difference << "/255\n";
            }
        }
        backend.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout << api << " reference captures=" << captures << ", max Raster/Hybrid MAE=" << maximum << "/255\n";
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
