#include "Backend.hpp"
#include "TestDevice.hpp"
#include "TestWindowHost.hpp"
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
    render::detail::PresentationWindow window;
    std::cout.setf(std::ios::unitbuf);
    try
    {
        const std::string           api    = argc > 1 ? TestAPI(argv[1]) : "metal";
        const std::filesystem::path assets = argc > 2 ? argv[2] : "assets";
        RendererConfig              config;
        config.api = api == "metal" ? GraphicsAPI::Metal : api == "vulkan" ? GraphicsAPI::Vulkan : GraphicsAPI::D3D12;
        config.useWarp = argc > 1 && TestUseWarp(argv[1]);
        if (!InitTestWindowHost())
            throw std::runtime_error("Window host initialization");
        window = OpenTestWindow(api, 160, 120, "Material validation");
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
        auto dir = std::filesystem::path("build/validation/material-alpha") / api;
        std::filesystem::create_directories(dir);
        auto draw = [&](const std::string& name) {
            PollTestWindow();
            int w, h;
            TestPixelSize(window, &w, &h);
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

        backend->SetClearColor({0, 0, 0, 1});
        backend->SetLighting(LightingMode::Unlit);
        Camera camera;
        camera.position = {0, 0, 3.6f};
        backend->SetViewCamera(camera);
        auto load = [&](const char* file) {
            auto data = detail::LoadGLB(assets / "MaterialSample" / (std::string(file) + ".glb"));
            backend->LoadModel(data);
        };
        auto material = [&](uint32_t slot = 0) { return backend->GetMaterials("").at(slot); };
        auto update   = [&](Material m, uint32_t slot = 0) { backend->SetMaterial(slot, m, ""); };
        auto energy   = [](const Image& im) {
            double sum = 0;
            for (size_t i = 0; i < im.pixels.size(); i += 4)
                sum += im.pixels[i] + im.pixels[i + 1] + im.pixels[i + 2];
            return sum / im.pixels.size();
        };
        load("Quad");
        auto front = draw("front");
        if (energy(front) < 2)
            throw std::runtime_error("Front face culled");
        auto m         = material();
        m.baseColor[3] = 0;
        update(m);
        auto opaque = draw("opaque-zero-alpha");
        if (Difference(front, opaque) > .05)
            throw std::runtime_error("OPAQUE alpha affected coverage");
        load("Back");
        auto back = draw("back-single");
        if (energy(back) > 1)
            throw std::runtime_error("Single-sided back visible");
        m             = material();
        m.doubleSided = true;
        update(m);
        auto two = draw("back-double");
        if (energy(two) < 2)
            throw std::runtime_error("Double-sided back missing");
        load("Mirrored");
        if (Difference(front, draw("mirrored-node")) > .05)
            throw std::runtime_error("Mirrored node winding");
        load("Mask");
        auto mask     = draw("mask");
        m             = material();
        m.alphaCutoff = 0;
        update(m);
        auto full = draw("mask-cutoff-zero");
        if (energy(mask) < 2 || energy(full) < energy(mask) * 1.5)
            throw std::runtime_error("MASK did not clip texture alpha");
        m.alphaCutoff  = .5f;
        m.baseColor[3] = .2f;
        update(m);
        if (energy(draw("mask-factor")) > 1)
            throw std::runtime_error("MASK factor ignored");
        auto original = material();
        m.alphaCutoff = 2;
        bool rejected = false;
        try
        {
            update(m);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected || material().alphaCutoff != original.alphaCutoff)
            throw std::runtime_error("Invalid update not atomic");
        load("Layers");
        auto layers = draw("layers-forward");
        load("LayersReverse");
        if (Difference(layers, draw("layers-reversed")) > .1)
            throw std::runtime_error("OIT primitive order dependence");
        RasterSettings raster;
        raster.pipeline = RasterPipeline::Deferred;
        backend->SetRasterSettings(raster);
        if (Difference(layers, draw("layers-deferred")) > .15)
            throw std::runtime_error("Deferred OIT parity");
        raster.pipeline = RasterPipeline::Forward;
        backend->SetRasterSettings(raster);
        for (auto method : {AAMethod::FXAA, AAMethod::SMAA, AAMethod::SSAA})
        {
            AntiAliasingSettings aa;
            aa.method = method;
            backend->SetAntiAliasing(aa);
            draw("layers-aa-" + std::to_string(int(method)));
        }
        AntiAliasingSettings aa;
        aa.method = AAMethod::Off;
        backend->SetAntiAliasing(aa);
        for (auto method : {AAMethod::MSAA, AAMethod::TAA})
        {
            aa.method = method;
            rejected  = false;
            try
            {
                backend->SetAntiAliasing(aa);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            if (!rejected)
                throw std::runtime_error("Unsupported BLEND AA accepted");
        }
        if (backend->GetCapabilities().pathTracing)
            throw std::runtime_error("Alpha ray capability incorrectly true");

        load("Transmission");
        backend->SetLighting(LightingMode::PBR);
        auto glass = draw("glass-forward");
        auto center = (glass.height / 2 * glass.width + glass.width / 2) * 4;
        if (glass.pixels[center] < glass.pixels[center + 1] + 35)
            throw std::runtime_error("Opaque receiver is not visible through glass");
        raster.pipeline = RasterPipeline::Deferred;
        backend->SetRasterSettings(raster);
        if (Difference(glass, draw("glass-deferred")) > .4)
            throw std::runtime_error("Deferred glass does not match Forward");
        raster.pipeline = RasterPipeline::Forward;
        backend->SetRasterSettings(raster);
        auto withoutPost = post;
        withoutPost.enabled = false;
        backend->SetPostProcessing(withoutPost);
        auto defaultGlass = draw("glass-default");
        if (defaultGlass.pixels[center] < defaultGlass.pixels[center + 1] + 35)
            throw std::runtime_error("Default raster path did not composite glass");
        backend->SetPostProcessing(post);
        backend->SetLighting(LightingMode::Unlit);

        auto matrix = [](float x, float y, float z, float tx, float ty, float tz) {
            return std::array<float, 16>{x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, tx, ty, tz, 1};
        };
        backend->SetScene(
          {{"quad", assets / "MaterialSample/Quad.glb", true}}, {{"mirror", "quad", matrix(-1, 1, 1, 0, 0, 0)}});
        if (Difference(front, draw("mirrored-instance")) > .05)
            throw std::runtime_error("Mirrored instance sidedness");
        backend->SetScene(
          {{"receiver", assets / "MaterialSample/Quad.glb", true},
           {"caster", assets / "MaterialSample/Mask.glb", true}},
          {{"receiver", "receiver", matrix(1.2f, 1.2f, 1, 0, 0, -.5f)},
           {"caster", "caster", matrix(.45f, .45f, 1, 0, 0, .3f)}});
        auto receiver      = backend->GetMaterials("receiver")[0];
        receiver.baseColor = {1, 1, 1, 1};
        backend->SetMaterial(0, receiver, "receiver");
        auto caster      = backend->GetMaterials("caster")[0];
        caster.baseColor = {1, 1, 1, 1};
        backend->SetMaterial(0, caster, "caster");
        backend->SetLighting(LightingMode::PBR);
        SceneLight sun;
        sun.id             = "sun";
        sun.direction      = {1, 0, 1};
        sun.intensity      = 4;
        sun.castShadow     = true;
        sun.shadowSoftness = 0;
        backend->SetLights({sun});
        SceneShadowSettings shadows;
        shadows.enabled = false;
        backend->SetShadows(shadows);
        auto off         = draw("shadow-off");
        shadows.enabled  = true;
        shadows.softness = 0;
        shadows.samples  = 1;
        backend->SetShadows(shadows);
        auto maskedShadow  = draw("shadow-mask");
        caster.alphaCutoff = 0;
        backend->SetMaterial(0, caster, "caster");
        auto solidShadow = draw("shadow-solid");
        if (Difference(off, maskedShadow) < .1 || Difference(maskedShadow, solidShadow) < .1)
            throw std::runtime_error("MASK shadow holes missing");
        backend.reset();
        CloseTestWindow(window);
        QuitTestWindowHost();
        std::cout << api << " material alpha validation passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        if (window)
            CloseTestWindow(window);
        QuitTestWindowHost();
        return 1;
    }
}
