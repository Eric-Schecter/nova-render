#include "Backend.hpp"
#include "TestDevice.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <stb_image_write.h>
#include <stdexcept>
using namespace render;
void Require(bool value, const char* text)
{
    if (!value)
        throw std::runtime_error(text);
}
int main(int argc, char** argv)
{
    GLFWwindow* window = nullptr;
    try
    {
        Require(argc == 3, "Expected API and assets");
        std::string api  = TestAPI(argv[1]);
        auto        path = std::filesystem::path(argv[2]) / "SceneSample/Red.glb";
        Require(glfwInit(), "GLFW init");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(320, 240, "GPU scene shadow validation", nullptr, nullptr);
        Require(window, "Window init");
        RendererConfig config;
        config.api     = api == "metal" ? GraphicsAPI::Metal : api == "dx12" ? GraphicsAPI::D3D12 : GraphicsAPI::Vulkan;
        config.useWarp = TestUseWarp(argv[1]);
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
        std::vector<SceneInstance> instances{{"ground", "box"}, {"caster", "box"}};
        auto&                      ground = instances[0].world;
        ground[0]                         = 2;
        ground[5]                         = .05f;
        ground[10]                        = 2;
        ground[13]                        = -.8f;
        auto& caster                      = instances[1].world;
        caster[0] = caster[10] = .3f;
        caster[5]              = 1;
        caster[13]             = .3f;
        backend->SetScene({{"box", path}}, instances);
        auto white      = backend->GetMaterials("box")[0];
        white.baseColor = {1, 1, 1, 1};
        white.metallic  = 0;
        white.roughness = .7f;
        backend->SetMaterial(0, white, "box");
        auto camera     = backend->GetCamera();
        camera.yaw      = 30;
        camera.pitch    = 25;
        camera.distance = 6;
        backend->SetCamera(camera);
        DirectionalLight light;
        light.direction = {-1, 2, 1};
        light.intensity = 8;
        backend->SetDirectionalLight(light);
        backend->SetLighting(LightingMode::PBR);
        backend->SetAntiAliasing({AAMethod::FXAA});
        auto draw = [&] {
            glfwPollEvents();
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            backend->Resize(w, h);
            Require(backend->DrawFrame() == FrameResult::Rendered, "Frame skipped");
            backend->WaitIdle();
            Require(!backend->ValidationErrorCount(), "GPU errors");
        };
        auto save = [&](const std::string& name) {
            auto& im = backend->GetRasterAAImage();
            std::filesystem::create_directories("validation/editor-shadows");
            auto file = "validation/editor-shadows/" + api + "-" + name + ".png";
            stbi_write_png(file.c_str(), im.width, im.height, 4, im.pixels.data(), im.width * 4);
        };
        draw();
        const auto off = backend->GetRasterAAImage();
        save("off");
        SceneShadowSettings settings;
        settings.enabled  = true;
        settings.softness = 0;
        backend->SetShadows(settings);
        draw();
        const auto hard = backend->GetRasterAAImage();
        save("hard");
        size_t darker = 0;
        for (size_t i = 0; i < off.pixels.size(); i += 4)
            if (
              int(off.pixels[i]) + off.pixels[i + 1] + off.pixels[i + 2]
              > int(hard.pixels[i]) + hard.pixels[i + 1] + hard.pixels[i + 2] + 12)
                darker++;
        Require(darker > 200, "GPU scene shadow missing");
        settings.softness = 4;
        settings.samples  = 16;
        backend->SetShadows(settings);
        draw();
        save("soft");
        Require(backend->GetRasterAAImage().pixels != hard.pixels, "Soft shadow equals hard shadow");
        settings.enabled = false;
        backend->SetShadows(settings);
        draw();
        Require(backend->GetRasterAAImage().pixels == off.pixels, "Shadow off changed baseline");
        settings.enabled  = true;
        settings.softness = 0;
        backend->SetShadows(settings);
        instances[1].castShadow = false;
        backend->SetScene({{"box", path}}, instances);
        draw();
        Require(backend->GetRasterAAImage().pixels == off.pixels, "castShadow false still casts");
        instances[1].castShadow = true;
        instances[1].world[12]  = 1;
        backend->SetScene({{"box", path}}, instances);
        draw();
        Require(backend->GetRasterAAImage().pixels != hard.pixels, "Moved caster retained stale shadow");
        auto bad      = settings;
        bad.samples   = 0;
        bool rejected = false;
        try
        {
            backend->SetShadows(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Require(
          rejected && backend->GetShadows().samples == settings.samples, "Invalid shadow settings replaced state");
        // Every collection directional light must use its own direction and cast flag.
        backend->ConfigurePathTracing(config.api, config);
        SceneLight sun;
        sun.id             = "sun";
        sun.direction      = {-1, 2, 1};
        sun.intensity      = 8;
        sun.shadowSoftness = 0;
        backend->SetLights({sun});
        draw();
        const auto collectionOff = backend->GetRasterAAImage();
        sun.castShadow           = true;
        backend->SetLights({sun});
        draw();
        const auto collectionHard = backend->GetRasterAAImage();
        Require(collectionHard.pixels != collectionOff.pixels, "Collection directional shadow missing");
        auto fill      = sun;
        fill.id        = "fill";
        fill.direction = {1, 2, 1};
        fill.color     = {.2f, .6f, 1};
        backend->SetLights({sun, fill});
        draw();
        const auto twoShadows = backend->GetRasterAAImage();
        fill.castShadow       = false;
        backend->SetLights({sun, fill});
        draw();
        Require(twoShadows.pixels != backend->GetRasterAAImage().pixels, "Second light shadow cast flag ignored");
        Require(
          backend->GetLightingCapabilities().directionalShadows && backend->GetLightingCapabilities().pointShadows
            && backend->GetLightingCapabilities().spotShadows,
          "Shadow capability mismatch");
        instances[1].world[12] = 0;
        backend->SetScene({{"box", path}}, instances);
        for (auto type : {LightType::Point, LightType::Spot})
        {
            const std::string label = type == LightType::Point ? "point" : "spot";
            SceneLight        local;
            local.id             = label;
            local.type           = type;
            local.position       = {-1, 3, 1};
            local.direction      = {1, -3, -1};
            local.intensity      = 35;
            local.innerConeAngle = .4f;
            local.outerConeAngle = 1.f;
            local.shadowSoftness = 0;
            backend->SetLights({local});
            draw();
            const auto unshadowed = backend->GetRasterAAImage();
            save(label + "-off");
            local.castShadow = true;
            backend->SetLights({local});
            draw();
            const auto shadowed = backend->GetRasterAAImage();
            save(label + "-hard");
            size_t pixels = 0;
            for (size_t i = 0; i < shadowed.pixels.size(); i += 4)
                if (
                  int(unshadowed.pixels[i]) + unshadowed.pixels[i + 1] + unshadowed.pixels[i + 2]
                  > int(shadowed.pixels[i]) + shadowed.pixels[i + 1] + shadowed.pixels[i + 2] + 12)
                    ++pixels;
            Require(pixels > 100, "Local light shadow missing");
            local.shadowSoftness = 4;
            backend->SetLights({local});
            draw();
            save(label + "-soft");
            Require(backend->GetRasterAAImage().pixels != shadowed.pixels, "Local soft shadow equals hard");
            settings.enabled = false;
            backend->SetShadows(settings);
            draw();
            Require(backend->GetRasterAAImage().pixels == unshadowed.pixels, "Global switch failed for local light");
            settings.enabled = true;
            backend->SetShadows(settings);
            local.shadowSoftness    = 0;
            instances[1].castShadow = false;
            backend->SetScene({{"box", path}}, instances);
            backend->SetLights({local});
            draw();
            Require(backend->GetRasterAAImage().pixels == unshadowed.pixels, "Excluded local caster still casts");
            instances[1].castShadow = true;
            instances[1].world[12]  = 1;
            backend->SetScene({{"box", path}}, instances);
            draw();
            Require(backend->GetRasterAAImage().pixels != shadowed.pixels, "Local caster movement ignored");
            // The blocker is beyond the source on rays from the visible floor center.
            local.position         = {1, 2, 0};
            local.direction        = {-1, -2, 0};
            local.innerConeAngle   = .5f;
            local.outerConeAngle   = 1.2f;
            instances[1].world[12] = 2;
            instances[1].world[13] = 4;
            backend->SetScene({{"box", path}}, instances);
            backend->SetLights({local});
            draw();
            const auto finite = backend->GetRasterAAImage();
            save(label + "-behind-source");
            instances[1].castShadow = false;
            backend->SetScene({{"box", path}}, instances);
            draw();
            Require(backend->GetRasterAAImage().pixels == finite.pixels, "Geometry behind local source casts shadow");
            local.range      = .1f;
            local.castShadow = false;
            backend->SetLights({local});
            draw();
            const auto outsideRange = backend->GetRasterAAImage();
            local.castShadow        = true;
            backend->SetLights({local});
            draw();
            Require(backend->GetRasterAAImage().pixels == outsideRange.pixels, "Shadows changed out-of-range lighting");
            if (type == LightType::Spot)
            {
                local.range      = 0;
                local.direction  = {0, 1, 0};
                local.castShadow = false;
                backend->SetLights({local});
                draw();
                const auto outsideCone = backend->GetRasterAAImage();
                local.castShadow       = true;
                backend->SetLights({local});
                draw();
                Require(backend->GetRasterAAImage().pixels == outsideCone.pixels, "Shadows changed outside spot cone");
            }
            instances[1].castShadow = true;
            instances[1].world[12]  = 0;
            instances[1].world[13]  = .3f;
            backend->SetScene({{"box", path}}, instances);
        }
        SceneLight point, spot;
        point.id             = "mixed-point";
        point.type           = LightType::Point;
        point.position       = {-1, 3, 1};
        point.intensity      = 25;
        point.color          = {1, .3f, .2f};
        point.castShadow     = true;
        point.shadowSoftness = 0;
        spot                 = point;
        spot.id              = "mixed-spot";
        spot.type            = LightType::Spot;
        spot.position        = {1, 3, 1};
        spot.direction       = {-1, -3, -1};
        spot.outerConeAngle  = 1;
        spot.color           = {.2f, .4f, 1};
        backend->SetLights({point, spot});
        draw();
        const auto mixed = backend->GetRasterAAImage();
        save("mixed-local-hard");
        point.castShadow = false;
        backend->SetLights({point, spot});
        draw();
        Require(backend->GetRasterAAImage().pixels != mixed.pixels, "Point shadow missing in mixed collection");
        point.castShadow = true;
        spot.castShadow  = false;
        backend->SetLights({point, spot});
        draw();
        Require(backend->GetRasterAAImage().pixels != mixed.pixels, "Spot shadow missing in mixed collection");
        backend.reset();
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        std::cout << "PASS GPU scene shadows, softness, cast flags and movement: " << api << "; shadow pixels "
                  << darker << '\n';
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
