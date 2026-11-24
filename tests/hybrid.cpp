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
        window = OpenTestWindow(api, 160, 120, "GPU presentation validation");
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
        auto dir = std::filesystem::path("build/validation/hybrid-shadow") / api;
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


        backend->LoadModel(detail::LoadGLB(assets / "DamagedHelmet/DamagedHelmet.glb"));
        backend->SetLighting(LightingMode::PBR);
        backend->SetRenderMode(RenderMode::Hybrid);
        auto caps = backend->GetHybridCapabilities();
        if (
          !caps.supported || !caps.shadows || !caps.softwareRayTracing || caps.hardwareRayTracing
          || !caps.ambientOcclusion || !caps.reflections || !caps.globalIllumination)
            throw std::runtime_error("Incorrect Hybrid capabilities");
        auto capture = [&](const std::string& name) {
            PollTestWindow();
            int w, h;
            TestPixelSize(window, &w, &h);
            backend->Resize(w, h);
            auto before = backend->RasterReadbackCount();
            if (backend->DrawFrame() != FrameResult::Rendered)
                throw std::runtime_error("Skipped Hybrid frame");
            backend->WaitIdle();
            if (backend->RasterReadbackCount() != before)
                throw std::runtime_error("Hybrid automatic readback");
            auto im = backend->CaptureRasterFrame();
            stbi_write_png(
              (dir / (name + ".png")).string().c_str(), im.width, im.height, 4, im.pixels.data(), im.width * 4);
            if (backend->ValidationErrorCount())
                throw std::runtime_error("GPU validation errors");
            return im;
        };
        HybridSettings settings;
        settings.shadows = false;
        backend->SetHybrid(settings);
        auto off         = capture("single-off");
        settings.shadows = true;
        backend->SetHybrid(settings);
        auto on     = capture("single-shadow");
        auto matrix = [](float x, float y, float z, float tx, float ty, float tz) {
            return std::array<float, 16>{x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, tx, ty, tz, 1};
        };
        backend->SetScene(
          {{"box", assets / "SceneSample/Red.glb"}},
          {{"ground", "box", matrix(2, .06f, 2, 0, -.8f, 0)}, {"caster", "box", matrix(.3f, 1, .3f, 0, 0, 0)}});
        auto material      = backend->GetMaterials("box")[0];
        material.baseColor = {.8f, .8f, .8f, 1};
        material.metallic  = 0;
        material.roughness = .7f;
        backend->SetMaterial(0, material, "box");
        OrbitCamera camera;
        camera.yaw      = 30;
        camera.pitch    = 25;
        camera.distance = 5.5f;
        backend->SetCamera(camera);
        SceneLight sun;
        sun.id             = "sun";
        sun.direction      = {-1, 2, 1};
        sun.intensity      = 4;
        sun.castShadow     = true;
        sun.shadowSoftness = 0;
        SceneLight point;
        point.id             = "point";
        point.type           = LightType::Point;
        point.position       = {-2, 2, 1};
        point.color          = {.2f, .3f, 1};
        point.intensity      = 6;
        point.castShadow     = true;
        point.shadowSoftness = 0;
        SceneLight spot;
        spot.id             = "spot";
        spot.type           = LightType::Spot;
        spot.position       = {2, 3, 2};
        spot.direction      = {-.3f, -1, -.2f};
        spot.intensity      = 8;
        spot.castShadow     = true;
        spot.shadowSoftness = 0;
        spot.color          = {1, .2f, .1f};
        spot.innerConeAngle = .4f;
        spot.outerConeAngle = .8f;
        SceneLight fill     = point;
        fill.id             = "fill";
        fill.castShadow     = false;
        fill.intensity      = .3f;
        SceneLight last     = sun;
        last.id             = "fifth";
        last.direction      = {1, 2, -1};
        last.intensity      = .5f;
        backend->SetLights({sun, point, spot, fill, last});
        settings.shadows = false;
        backend->SetHybrid(settings);
        auto sceneOff    = capture("scene-off");
        settings.shadows = true;
        backend->SetHybrid(settings);
        auto sceneShadow = capture("scene-shadow");
        if (Difference(sceneOff, sceneShadow) < .1)
            throw std::runtime_error("Missing Hybrid shadows");
        settings.halfResolutionShadows = true;
        backend->SetHybrid(settings);
        const auto compactShadow = capture("shadow-compact");
        if (Difference(sceneShadow, compactShadow) > .8 || Difference(sceneOff, compactShadow) < .1)
            throw std::runtime_error("Compact shadows lost five-light visibility");
        if (backend->GetHybridCapabilities().sparseShadowFallback)
        {
            settings.sparseShadowFallback = true;
            backend->SetHybrid(settings);
            if (Difference(compactShadow, capture("shadow-sparse")) > .02)
                throw std::runtime_error("Sparse fallback changed five-light visibility");
            settings.sparseShadowFallback = false;
        }
        settings.halfResolutionShadows = false;
        settings.blueNoiseShadows = true;
        backend->SetHybrid(settings);
        if (Difference(sceneShadow, capture("shadow-blue-hard")) > .01)
            throw std::runtime_error("Blue noise changed hard shadows");
        settings.blueNoiseShadows = false;

        for (uint32_t i : {0u, 1u, 2u, 4u})
        {
            settings.view        = HybridView::Shadows;
            settings.shadowLight = i;
            backend->SetHybrid(settings);
            capture("visibility-" + std::to_string(i));
        }
        settings.view = HybridView::Final;
        backend->SetHybrid(settings);
        capture("scene-final");
        settings.shadowTemporalReconstruction = true;
        backend->SetHybrid(settings);
        for (int frame = 0; frame < 3; ++frame)
        {
            const auto resolved = capture("shadow-history-" + std::to_string(frame));
            if (Difference(resolved, sceneShadow) > .2)
                throw std::runtime_error("Shadow history changed hard shadows in a five-light scene");
        }
        if (backend->GetHybridHistoryFrames() != 3)
            throw std::runtime_error("Independent shadow history did not accumulate");
        auto shadowCamera = backend->GetViewCamera();
        shadowCamera.position[0] += .05f;
        backend->SetViewCamera(shadowCamera);
        capture("shadow-history-motion");
        if (backend->GetHybridHistoryFrames() != 4)
            throw std::runtime_error("Camera motion reset independent shadow history");
        shadowCamera.verticalFov += 1;
        backend->SetViewCamera(shadowCamera);
        if (backend->GetHybridHistoryFrames() != 0)
            throw std::runtime_error("Lens change retained independent shadow history");
        backend->SetCamera(camera);
        settings.shadowSVGF = true;
        backend->SetHybrid(settings);
        capture("shadow-svgf-first");
        if (Difference(sceneShadow, capture("shadow-svgf-hard")) > .2)
            throw std::runtime_error("Shadow SVGF changed static hard shadows");
        settings.shadowSVGF = false;
        settings.shadowTemporalReconstruction = false;
        backend->SetHybrid(settings);
        AntiAliasingSettings aa;
        aa.method     = AAMethod::TAA;
        bool rejected = false;
        try
        {
            backend->SetAntiAliasing(aa);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected || backend->GetRenderMode() != RenderMode::Hybrid)
            throw std::runtime_error("Hybrid AA validation");
        backend->SetRenderMode(RenderMode::Raster);
        SceneShadowSettings shadow;
        shadow.enabled  = true;
        shadow.softness = 0;
        shadow.samples  = 4;
        backend->SetShadows(shadow);
        auto raster = draw("raster-shadow");
        if (Difference(sceneShadow, raster) > .2)
            throw std::runtime_error("Hybrid/Raster light composition mismatch");
        settings.view = HybridView::AmbientOcclusion;
        backend->SetHybrid(settings);
        auto retainedView = draw("raster-retained-hybrid-view");
        if (Difference(raster, retainedView) > .01)
            throw std::runtime_error("Inactive Hybrid diagnostic changed Raster output");
        settings.view = HybridView::Final;
        backend->SetRenderMode(RenderMode::Hybrid);
        settings.ambientOcclusion = true;
        settings.aoRadius         = 2;
        settings.aoSamples        = 16;
        backend->SetHybrid(settings);
        auto ao = capture("ao-final");
        if (Difference(sceneShadow, ao) < .05)
            throw std::runtime_error("Missing Hybrid AO");
        settings.halfResolutionAO = true;
        backend->SetHybrid(settings);
        const auto firstAO = capture("ao-half-first");
        const auto halfAO = capture("ao-half-history");
        if (backend->GetHybridHistoryFrames() != 2 || Difference(sceneShadow, halfAO) < .05
            || Difference(ao, halfAO) > 5)
            throw std::runtime_error("Independent half-resolution AO failed");
        backend->ResetHybridHistory();
        const auto resetAO = capture("ao-half-reset");
        if (Difference(resetAO, firstAO) > .01 || backend->GetHybridHistoryFrames() != 1)
            throw std::runtime_error("AO history did not reset");
        settings.blueNoiseAO = true;
        backend->SetHybrid(settings);
        const auto blueAO = capture("ao-half-blue-noise");
        if (Difference(blueAO, firstAO) <= .0001)
            throw std::runtime_error("Blue-noise AO did not change ray sampling");
        settings.blueNoiseAO = false;
        settings.halfResolutionAO = false;
        settings.view = HybridView::AmbientOcclusion;
        backend->SetHybrid(settings);
        capture("ao-visibility");
        settings.view             = HybridView::Final;
        settings.ambientOcclusion = false;
        settings.reflections      = false;
        material.metallic         = .9f;
        material.roughness        = .08f;
        backend->SetMaterial(0, material, "box");
        backend->SetHybrid(settings);
        auto reflectionOff         = capture("reflection-off");
        settings.reflections       = true;
        settings.reflectionSamples = 4;
        backend->SetHybrid(settings);
        auto reflection = capture("reflection-final");
        if (Difference(reflectionOff, reflection) < .05)
            throw std::runtime_error("Missing Hybrid reflection");
        settings.view = HybridView::Reflections;
        backend->SetHybrid(settings);
        capture("reflection-only");
        settings.halfResolutionReflections = true;
        backend->SetHybrid(settings);
        capture("reflection-half-only");
        settings.view = HybridView::Final;
        backend->SetHybrid(settings);
        auto halfReflection = capture("reflection-half-final");
        if (Difference(reflectionOff, halfReflection) < .05 || Difference(reflection, halfReflection) > 5)
            throw std::runtime_error("Half-resolution reflection did not preserve reflection lighting");
        settings.reflectionDenoising = true;
        backend->SetHybrid(settings);
        auto denoisedFirst = capture("reflection-denoised-first");
        settings.blueNoiseReflections = true;
        backend->SetHybrid(settings);
        const auto blueReflection = capture("reflection-blue-noise");
        if (Difference(blueReflection, denoisedFirst) <= .0001)
            throw std::runtime_error("Blue noise did not change half-resolution GGX sampling");
        settings.blueNoiseReflections = false;
        backend->SetHybrid(settings);

        auto denoised = denoisedFirst;
        for (int i = 0; i < 8; ++i)
            denoised = capture("reflection-denoised-" + std::to_string(i));
        if (Difference(reflectionOff, denoised) < .05 || Difference(reflection, denoised) > 5
            || Difference(denoisedFirst, denoised) < .001)
            throw std::runtime_error("Independent reflection denoising did not accumulate");
        settings.reflectionSVGF = true;
        backend->SetHybrid(settings);
        capture("reflection-svgf-first");
        auto svgf = capture("reflection-svgf-history");
        if (Difference(reflectionOff, svgf) < .05 || Difference(reflection, svgf) > 5)
            throw std::runtime_error("Reflection SVGF did not preserve lighting");
        settings.reflectionSVGF = false;
        settings.reflectionDenoising = false;
        settings.halfResolutionReflections = false;
        settings.ambientOcclusion       = true;
        settings.aoSamples              = 4;
        settings.reflectionSamples      = 1;
        settings.temporalReconstruction = true;
        backend->SetHybrid(settings);
        settings.historyWeight = 0;
        backend->SetHybrid(settings);
        std::vector<double> reference;
        for (int f = 1; f <= 16; ++f)
        {
            auto raw = capture("temporal-raw-" + std::to_string(f));
            if (reference.empty())
                reference.resize(raw.pixels.size());
            for (size_t i = 0; i < reference.size(); ++i)
                reference[i] += double(raw.pixels[i]) / 16;
        }
        settings.historyWeight = .9f;
        backend->SetHybrid(settings);
        auto  first = capture("temporal-first");
        Image resolved;
        for (int f = 2; f <= 16; ++f)
            resolved = capture("temporal-" + std::to_string(f));
        if (backend->GetHybridHistoryFrames() != 16 || Difference(first, resolved) < .01)
            throw std::runtime_error("Missing Hybrid temporal history");
        auto mse = [&](const Image& im) {
            double sum = 0;
            for (size_t i = 0; i < reference.size(); ++i)
                sum += (im.pixels[i] - reference[i]) * (im.pixels[i] - reference[i]);
            return sum / reference.size();
        };
        std::cout << "Temporal reference MSE: first=" << mse(first) << "; resolved=" << mse(resolved) << '\n';
        if (mse(resolved) >= mse(first))
            throw std::runtime_error("Temporal reconstruction did not reduce reference error");
        camera.yaw += 2;
        backend->SetCamera(camera);
        capture("temporal-motion");
        if (backend->GetHybridHistoryFrames() != 17)
            throw std::runtime_error("Small movement lost history");
        camera.yaw += 40;
        backend->SetCamera(camera);
        auto cut = capture("temporal-cut");
        if (backend->GetHybridHistoryFrames() != 1)
            throw std::runtime_error("Camera cut retained history");
        backend->ResetHybridHistory();
        if (backend->GetHybridHistoryFrames() != 0)
            throw std::runtime_error("Explicit reset failed");
        auto reset = capture("temporal-reset");
        if (Difference(cut, reset) > .01)
            throw std::runtime_error("Reset reused stale history");
        auto full = backend->GetViewCamera();
        full.position[0] += .1f;
        backend->SetViewCamera(full);
        capture("temporal-full-camera");
        if (backend->GetHybridHistoryFrames() != 2)
            throw std::runtime_error("Full camera movement lost history");
        full.verticalFov += 1;
        backend->SetViewCamera(full);
        if (backend->GetHybridHistoryFrames() != 0)
            throw std::runtime_error("Lens change retained history");
        settings.historyWeight = 2;
        bool invalidHybrid     = false;
        try
        {
            backend->SetHybrid(settings);
        }
        catch (const std::invalid_argument&)
        {
            invalidHybrid = true;
        }
        if (!invalidHybrid || backend->GetHybrid().historyWeight != .9f)
            throw std::runtime_error("Invalid Hybrid history mutated settings");
        aa.method = AAMethod::SSAA;
        backend->SetAntiAliasing(aa);
        for (int f = 0; f < 4; ++f)
            capture("temporal-ssaa");
        if (backend->GetHybridHistoryFrames() != 4)
            throw std::runtime_error("Hybrid SSAA history failed");
        aa.method = AAMethod::Off;
        backend->SetAntiAliasing(aa);
        HybridSettings alphaSettings;
        backend->SetHybrid(alphaSettings);
        backend->LoadModel(detail::LoadGLB(assets / "MaterialSample/Layers.glb"));
        backend->SetLighting(LightingMode::Unlit);
        backend->SetCamera(OrbitCamera{});
        auto alphaBase                       = capture("temporal-alpha-base");
        alphaSettings.temporalReconstruction = true;
        backend->SetHybrid(alphaSettings);
        Image alphaResolved;
        for (int f = 0; f < 4; ++f)
            alphaResolved = capture("temporal-alpha");
        if (Difference(alphaBase, alphaResolved) > .05)
            throw std::runtime_error("Temporal OIT changed transparency");
        backend->SetScene(
          {{"mirror", assets / "SceneSample/Red.glb"}, {"helmet", assets / "DamagedHelmet/DamagedHelmet.glb"}},
          {{"ground", "mirror", matrix(2, .06f, 2, 0, -.9f, 0)},
           {"subject", "helmet", matrix(.65f, .65f, .65f, 0, .05f, 0)}});
        backend->SetLighting(LightingMode::PBR);
        backend->SetCamera(camera);
        backend->SetDirectionalLight(DirectionalLight{});
        auto mirror      = backend->GetMaterials("mirror")[0];
        mirror.baseColor = {.85f, .85f, .85f, 1};
        mirror.metallic  = 1;
        mirror.roughness = .04f;
        backend->SetMaterial(0, mirror, "mirror");
        HybridSettings hitMaterials;
        hitMaterials.reflections = true;
        hitMaterials.view        = HybridView::Reflections;
        backend->SetHybrid(hitMaterials);
        auto helmet              = backend->GetMaterials("helmet")[0];
        helmet.normalScale       = 1;
        helmet.occlusionStrength = 1;
        backend->SetMaterial(0, helmet, "helmet");
        auto mapped        = capture("reflected-material-mapped");
        helmet.normalScale = 0;
        backend->SetMaterial(0, helmet, "helmet");
        auto flat                = capture("reflected-material-flat");
        helmet.occlusionStrength = 0;
        backend->SetMaterial(0, helmet, "helmet");
        auto clearAO     = capture("reflected-material-no-ao");
        auto groundDelta = [](const Image& a, const Image& b) {
            size_t changed = 0;
            for (size_t i = size_t(a.height * .65f) * a.width * 4; i < a.pixels.size(); ++i)
                changed += std::abs(int(a.pixels[i]) - int(b.pixels[i])) > 2;
            return changed;
        };
        if (groundDelta(mapped, flat) < 20 || groundDelta(flat, clearAO) < 20)
            throw std::runtime_error("Reflected helmet lost normal map or material AO");
        auto reprojection                      = hitMaterials;
        reprojection.view                      = HybridView::Final;
        reprojection.halfResolutionReflections = true;
        reprojection.reflectionDenoising       = true;
        reprojection.reflectionHitReprojection = true;
        backend->SetHybrid(reprojection);
        capture("reflection-hit-history-first");
        auto movingCamera = backend->GetViewCamera();
        movingCamera.position[0] += .05f;
        backend->SetViewCamera(movingCamera);
        const auto hitHistory = capture("reflection-hit-history-motion");
        if (backend->GetHybridHistoryFrames() != 2)
            throw std::runtime_error("Reflection hit reprojection lost history under camera motion");
        backend->ResetHybridHistory();
        const auto hitReset = capture("reflection-hit-history-reset");
        if (backend->GetHybridHistoryFrames() != 1 || groundDelta(hitHistory, hitReset) < 20)
            throw std::runtime_error("No reflection history reached the moving mirror");
        backend->SetCamera(camera);
        auto polishedHelmet = helmet;
        polishedHelmet.roughness = .04f;
        backend->SetMaterial(0, polishedHelmet, "helmet");
        reprojection.mirrorReflections = true;
        reprojection.reflectionHitReprojection = false;
        reprojection.reflectionSamples = 1;
        backend->SetHybrid(reprojection);
        const auto oneMirrorRay = capture("mirror-shortcut-one");
        reprojection.reflectionSamples = 8;
        backend->SetHybrid(reprojection);
        const auto eightMirrorRays = capture("mirror-shortcut-eight");
        if (Difference(oneMirrorRay, eightMirrorRays) != 0)
            throw std::runtime_error("Mirror reflection depended on configured ray count");
        const Matrix4 coatFloor{2, 0, 0, 0, 0, 0, -2, 0, 0, 1, 0, 0, 0, -.84f, 0, 1};
        backend->SetScene(
          {{"mirror", assets / "MaterialSample/Quad.glb"}, {"helmet", assets / "DamagedHelmet/DamagedHelmet.glb"}},
          {{"ground", "mirror", coatFloor}, {"subject", "helmet", matrix(.65f, .65f, .65f, 0, .05f, 0)}});
        backend->SetMaterial(0, helmet, "helmet");
        mirror.clearcoatFactor = 1;
        mirror.clearcoatRoughnessFactor = .02f;
        mirror.roughness = .62f;
        backend->SetMaterial(0, mirror, "mirror");
        hitMaterials.clearcoatReflections = false;
        backend->SetHybrid(hitMaterials);
        auto coatEnvironment = capture("coat-environment");
        hitMaterials.clearcoatReflections = true;
        backend->SetHybrid(hitMaterials);
        auto coatSharp = capture("coat-traced");
        mirror.clearcoatRoughnessFactor = .7f;
        backend->SetMaterial(0, mirror, "mirror");
        auto coatRough = capture("coat-rough");
        mirror.clearcoatRoughnessFactor = .02f;
        mirror.clearcoatNormalTexture = Image{1, 1, {210, 128, 218, 255}};
        backend->SetMaterial(0, mirror, "mirror");
        auto coatNormal = capture("coat-normal");
        if (groundDelta(coatEnvironment, coatSharp) < 20 || groundDelta(coatSharp, coatRough) < 20
            || groundDelta(coatSharp, coatNormal) < 20)
            throw std::runtime_error("Hybrid coat ray did not use its own normal and roughness");
        hitMaterials.halfResolutionReflections = true;
        hitMaterials.reflectionDenoising = true;
        backend->SetHybrid(hitMaterials);
        auto coatHalf = capture("coat-half");
        for (int f = 0; f < 4; ++f)
            coatHalf = capture("coat-half");
        if (groundDelta(coatEnvironment, coatHalf) < 20)
            throw std::runtime_error("Half-resolution denoising lost the traced coat");
        mirror.clearcoatFactor = 0;
        mirror.clearcoatNormalTexture = {};

        backend->SetScene(
          {{"mirror", assets / "SceneSample/Red.glb"}, {"wall", assets / "SceneSample/Red.glb"}},
          {{"ground", "mirror", matrix(2, .06f, 2, 0, -.9f, 0)}, {"wall", "wall", matrix(2, .9f, .06f, 0, 0, -1)}});
        camera.yaw = 30;
        backend->SetCamera(camera);
        DirectionalLight bounceLight;
        bounceLight.intensity = 1;
        backend->SetDirectionalLight(bounceLight);
        mirror.baseColor = {.45f, .45f, .45f, 1};
        mirror.metallic  = 0;
        mirror.roughness = .7f;
        backend->SetMaterial(0, mirror, "mirror");
        auto redWall      = backend->GetMaterials("wall")[0];
        redWall.baseColor = {1, 0, 0, 1};
        redWall.metallic  = 0;
        backend->SetMaterial(0, redWall, "wall");
        HybridSettings bounce;
        backend->SetHybrid(bounce);
        auto noBounce        = capture("diffuse-bounce-off");
        bounce.diffuseBounce = true;
        backend->SetHybrid(bounce);
        auto   indirect   = capture("diffuse-bounce-on");
        size_t colorSpill = 0;
        for (size_t i = size_t(indirect.height * .65f) * indirect.width * 4; i < indirect.pixels.size(); i += 4)
            colorSpill += int(indirect.pixels[i]) - int(noBounce.pixels[i])
                          > int(indirect.pixels[i + 1]) - int(noBounce.pixels[i + 1]) + 3;
        if (colorSpill < 20)
            throw std::runtime_error("Diffuse bounce did not transfer red light to the receiver");
        SceneLight rectangle;
        rectangle.id          = "softbox";
        rectangle.type        = LightType::Rectangle;
        rectangle.position    = {1, 1.2f, -1.6f};
        rectangle.direction   = {0, -.6f, 1};
        rectangle.up          = {0, 1, 0};
        rectangle.width       = 2.5f;
        rectangle.height      = .6f;
        rectangle.intensity   = 7;
        rectangle.castShadow  = true;
        HybridSettings rectangleSettings;
        rectangleSettings.shadows = false;
        backend->SetHybrid(rectangleSettings);
        backend->SetLights({});
        auto dark = capture("rectangle-off");
        backend->SetLights({rectangle});
        auto lit = capture("rectangle-unshadowed");
        rectangleSettings.shadows = true;
        backend->SetHybrid(rectangleSettings);
        auto rectangleShadow = capture("rectangle-shadowed");
        rectangle.width = .25f;
        backend->SetLights({rectangle});
        auto narrow = capture("rectangle-narrow");
        if (!backend->GetLightingCapabilities().rectangleShadows || Difference(dark, lit) < 1
            || Difference(lit, rectangleShadow) < .1 || Difference(rectangleShadow, narrow) < .1)
            throw std::runtime_error("Rectangle size, lighting or finite shadows did not affect the scene");
        rectangle.up = rectangle.direction;
        bool rejectedUp = false;
        try
        {
            backend->SetLights({rectangle});
        }
        catch (const std::invalid_argument&)
        {
            rejectedUp = true;
        }
        if (!rejectedUp || Difference(narrow, capture("rectangle-unchanged")) > .01)
            throw std::runtime_error("Invalid rectangle orientation corrupted the light collection");

        backend.reset();
        CloseTestWindow(window);
        QuitTestWindowHost();
        std::cout << api << " Hybrid shadows, independent lights, GPU presentation and Raster parity passed\n";
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
