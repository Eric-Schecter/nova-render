#include "Backend.hpp"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <stb_image_write.h>
#include <stdexcept>
using namespace render;
void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char** argv)
{
    GLFWwindow* window = nullptr;
    try
    {
        Require(argc == 3, "Expected API and asset directory");
        std::string           api    = argv[1];
        std::filesystem::path path   = std::filesystem::path(argv[2]) / "MaterialSample/Dual.glb";
        auto                  source = detail::LoadGLB(path);
        Require(source->materials.size() == 2, "Missing slots");
        Require(
          source->vertices[0].position[3] == 1 && source->vertices[36].position[3] == 2, "Incorrect slot assignment");
        Require(glfwInit(), "GLFW init");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(320, 240, "Material validation", nullptr, nullptr);
        Require(window, "Window failed");
        RendererConfig config;
        config.api     = api == "metal" ? GraphicsAPI::Metal : api == "dx12" ? GraphicsAPI::D3D12 : GraphicsAPI::Vulkan;
        config.useWarp = api == "dx12";
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
        Require(bool(backend), "Backend unavailable");
        backend->ConfigurePathTracing(config.api, config);
        backend->LoadModel(source);
        Camera camera;
        camera.position           = {0, 0, 4};
        camera.projection         = Projection::Orthographic;
        camera.orthographicHeight = 2.6f;
        backend->SetViewCamera(camera);
        auto draw = [&]() {
            glfwPollEvents();
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            backend->Resize(w, h);
            Require(backend->DrawFrame() == FrameResult::Rendered, "Skipped frame");
            backend->WaitIdle();
            Require(!backend->ValidationErrorCount(), "GPU errors");
        };
        auto count = [&](int channel, bool left) {
            const auto& im = backend->GetRasterAAImage();
            size_t      n  = 0;
            for (uint32_t y = 0; y < im.height; y++)
                for (uint32_t x = 0; x < im.width; x++)
                {
                    auto* p = &im.pixels[(size_t(y) * im.width + x) * 4];
                    if (
                      (x < im.width / 2) == left && p[channel] > 120 && p[channel] > p[(channel + 1) % 3] * 2
                      && p[channel] > p[(channel + 2) % 3] * 2)
                        n++;
                }
            return n;
        };
        draw();
        backend->SetAntiAliasing({AAMethod::FXAA});
        draw();
        Require(count(0, true) > 500 && count(2, false) > 500, "Multislot colors missing");
        FogSettings fog;
        fog.mode         = FogMode::Linear;
        fog.nearDistance = 0;
        fog.farDistance  = 1;
        fog.color        = {0, 1, 0};
        backend->SetFog(fog);
        draw();
        Require(count(1, true) > 500 && count(1, false) > 500, "Linear fog did not affect both materials");
        fog.mode    = FogMode::ExponentialSquared;
        fog.density = 1;
        backend->SetFog(fog);
        draw();
        Require(count(1, false) > 500, "Exponential fog missing");
        auto badFog        = fog;
        badFog.farDistance = 0;
        bool fogRejected   = false;
        try
        {
            backend->SetFog(badFog);
        }
        catch (const std::invalid_argument&)
        {
            fogRejected = true;
        }
        Require(fogRejected && backend->GetFog().density == 1, "Fog validation not transactional");
        fog.mode         = FogMode::Linear;
        fog.color        = {.2f, .4f, .6f};
        fog.nearDistance = 0;
        fog.farDistance  = 8;
        backend->SetFog(fog);
        draw();
        std::filesystem::create_directories("validation/materials");
        {
            std::ofstream pixels("validation/materials/" + api + "-fog-pixels.bin", std::ios::binary);
            const auto&   im = backend->GetRasterAAImage();
            for (float x : {.33f, .67f})
            {
                const size_t at = (size_t(im.height / 2) * im.width + uint32_t(im.width * x)) * 4;
                pixels.write(reinterpret_cast<const char*>(im.pixels.data() + at), 3);
            }
        }
        backend->SetFog({});
        draw();
        Require(count(0, true) > 500 && count(2, false) > 500, "Fog off did not restore materials");
        auto slots         = backend->GetMaterials("");
        slots[0].baseColor = {0, 1, 0, 1};
        slots[0].roughness = .2f;
        slots[0].metallic  = .8f;
        backend->SetMaterial(0, slots[0], "");
        draw();
        Require(count(1, true) > 500 && count(2, false) > 500, "Slot update leaked");
        Require(backend->GetMaterials("")[0].metallic == .8f, "Factor not retained");
        auto invalid      = slots[0];
        invalid.roughness = -1;
        bool rejected     = false;
        try
        {
            backend->SetMaterial(0, invalid, "");
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Require(rejected, "Invalid factor accepted");
        draw();
        Require(count(1, true) > 500, "Failed update corrupted state");
        slots[0].baseColor            = {1, 1, 1, 1};
        const auto          imageFile = std::filesystem::temp_directory_path() / ("nova-material-" + api + ".png");
        const unsigned char pixel[]   = {255, 0, 0, 255};
        Require(stbi_write_png(imageFile.string().c_str(), 1, 1, 4, pixel, 4) != 0, "PNG fixture write failed");
        slots[0].baseColorTexture = LoadMaterialImage(imageFile);
        std::filesystem::remove(imageFile);
        Require(
          slots[0].baseColorTexture->pixels == std::vector<unsigned char>(pixel, pixel + 4),
          "Image decode changed RGBA bytes");
        backend->SetMaterial(0, slots[0], "");
        draw();
        Require(count(0, true) > 500 && count(2, false) > 500, "Runtime image upload failed");
        slots[0].baseColorTexture.reset();
        backend->SetMaterial(0, slots[0], "");
        draw();
        Require(count(0, true) == 0, "Texture removal failed");
        for (unsigned method = 1; method <= 5; method++)
        {
            backend->SetAntiAliasing({AAMethod(method)});
            draw();
            Require(count(2, false) > 500, "AA lost second slot");
        }
        backend->SetScene({{"dual", path}}, {{"one", "dual"}});
        auto hit = backend->scene.Pick({{-.6f, 0, 4}, {0, 0, -1}});
        Require(bool(hit), "Scene pick missed");
        auto material      = backend->GetMaterials("dual")[1];
        material.baseColor = {0, 1, 0, 1};
        backend->SetMaterial(1, material, "dual");
        draw();
        Require(count(0, true) > 500 && count(1, false) > 500, "Scene material edit failed");
        Require(
          backend->scene.Pick({{-.6f, 0, 4}, {0, 0, -1}})->distance == hit->distance, "Material edit changed picking");
        backend->LoadModel(source);
        backend->SetLighting(LightingMode::PBR);
        backend->SetAntiAliasing({AAMethod::Off});
        if (backend->GetCapabilities().pathTracing)
        {
            backend->SetRenderMode(RenderMode::PathTracing);
            draw();
            Require(backend->GetAccumulatedSamples() > 0, "Multislot ray tracing failed");
            detail::PathSession session;
            session.Capabilities(config.api, config);
            PathTracingSettings settings;
            settings.resolutionScale = 1;
            settings.maxBounces      = 2;
            auto        uniforms     = detail::MakeSceneUniforms(*source, camera, 1.f, LightingMode::PBR);
            const auto& image        = session.Render(source, nullptr, uniforms, settings, {0, 0, 0, 1}, 96, 96);
            size_t      red = 0, blue = 0;
            for (size_t i = 0; i < image.pixels.size(); i += 4)
            {
                auto* p = &image.pixels[i];
                if (p[0] > p[2] * 2 && p[0] > 80)
                    red++;
                if (p[2] > p[0] * 2 && p[2] > 80)
                    blue++;
            }
            Require(red > 100 && blue > 100, "Ray tracer lost independent material slots");
        }
        backend->WaitIdle();
        backend.reset();
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        std::cout << "PASS material slots, runtime factors/maps, AA, scene picking and ray tracing: " << api << '\n';
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
