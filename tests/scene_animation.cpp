#include "Backend.hpp"
#include "TestDevice.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
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
        auto dir = std::filesystem::path("build/validation/scene-animation") / api;
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


        auto world = [](float x, float y, float z) { return Matrix4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1}; };
        std::vector<SceneAsset> sources{
          {"rig", assets / "SkinningRobot/SkinningRobot.glb"}, {"box", assets / "SceneSample/Red.glb"}};
        std::vector<SceneInstance> instances{
          {"a", "rig", world(-1.2f, 0, 0)}, {"b", "rig", world(1.2f, 0, 0)}, {"static", "box", world(0, -2, 0)}};
        backend->SetScene(sources, instances);
        backend->SetLighting(LightingMode::Unlit);
        OrbitCamera camera;
        camera.distance = 6;
        backend->SetCamera(camera);
        auto clips = backend->scene.Animations("rig");
        if (clips.size() != 2 || backend->scene.Animation("a").playing)
            throw std::runtime_error("Missing independent clips/manual clock");
        const auto vertexCount = backend->scene.model->vertices.size();
        auto       base        = draw("bind");
        auto       boundsA = backend->scene.InstanceBounds("a"), boundsB = backend->scene.InstanceBounds("b");
        auto       pickGrid = [&] {
            std::vector<double> hits;
            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 20; ++x)
                {
                    Ray ray;
                    ray.origin     = {-.1f + x * .14f, -1.4f + y * .15f, 10};
                    ray.direction  = {0, 0, -1};
                    ray.includeIds = {"b"};
                    auto hit       = backend->scene.Pick(ray);
                    hits.push_back(hit ? hit->distance : -1);
                }
            return hits;
        };
        auto           bindPicks = pickGrid();
        AnimationState state;
        state.clip    = 1;
        state.time    = clips[1].duration * .4f;
        state.playing = false;
        backend->SetSceneAnimation("b", state);
        auto   posed     = draw("pose");
        auto   posePicks = pickGrid();
        size_t changed   = 0;
        for (size_t i = 0; i < bindPicks.size(); ++i)
            if (std::abs(bindPicks[i] - posePicks[i]) > 1e-5)
                ++changed;
        auto nextBounds = backend->scene.InstanceBounds("b");
        if (
          !changed || !boundsA || !boundsB || !nextBounds
          || (boundsB->minimum == nextBounds->minimum && boundsB->maximum == nextBounds->maximum))
            throw std::runtime_error("Pose picking/bounds stayed in bind pose");
        auto stillA = backend->scene.InstanceBounds("a");
        if (boundsA->minimum != stillA->minimum || boundsA->maximum != stillA->maximum)
            throw std::runtime_error("Instance A bounds contaminated");
        double left = 0, right = 0;
        for (uint32_t y = 0; y < base.height; ++y)
            for (uint32_t x = 0; x < base.width; ++x)
                for (int c = 0; c < 3; ++c)
                {
                    const size_t i = (y * base.width + x) * 4 + c;
                    (x < base.width / 2 ? left : right) += std::abs(int(base.pixels[i]) - int(posed.pixels[i]));
                }
        if (left != 0 || right < 100)
            throw std::runtime_error(
              "GPU instance poses missing or cross-contaminated: " + std::to_string(left) + "/"
              + std::to_string(right));
        if (backend->scene.Animation("a").time != 0 || backend->scene.model->vertices.size() != vertexCount)
            throw std::runtime_error("Pose state/geometry sharing failed");
        auto previous = backend->scene.Animation("b");
        state.playing = true;
        state.speed   = 2;
        backend->SetSceneAnimation("b", state);
        backend->scene.UpdateAnimations(.1f);
        float expected = std::fmod(previous.time + .2f, clips[1].duration);
        if (std::abs(backend->scene.Animation("b").time - expected) > 1e-5 || backend->scene.Animation("a").time != 0)
            throw std::runtime_error("Explicit animation clock failed");
        draw("advanced");
        state.time    = clips[1].duration;
        state.playing = false;
        backend->SetSceneAnimation("b", state);
        backend->scene.UpdateAnimations(0);
        if (backend->scene.Animation("b").time != state.time)
            throw std::runtime_error("Paused end seek wrapped");
        for (float invalid : {-1.f, std::numeric_limits<float>::quiet_NaN()})
        {
            bool rejected = false;
            try
            {
                backend->scene.UpdateAnimations(invalid);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            if (!rejected || backend->scene.Animation("b").time != state.time)
                throw std::runtime_error("Invalid clock changed state");
        }
        auto bad      = state;
        bad.speed     = 5;
        bool rejected = false;
        try
        {
            backend->SetSceneAnimation("b", bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected || backend->scene.Animation("b").speed != state.speed)
            throw std::runtime_error("Invalid state changed pose");
        instances.push_back({"extra", "box", world(10, 0, 0)});
        backend->SetScene(sources, instances);
        if (backend->scene.Animation("b").time != state.time)
            throw std::runtime_error("SetScene lost instance state");
        auto material      = backend->GetMaterials("box")[0];
        material.roughness = .4f;
        backend->SetMaterial(0, material, "box");
        if (backend->scene.Animation("b").time != state.time)
            throw std::runtime_error("Material edit lost state");
        auto caps = backend->GetAnimationCapabilities();
        if (
          !caps.skeletal || !caps.instancePlayback || !caps.gpuSkinning || caps.morph || !caps.dynamicShadows
          || !caps.posedPicking || !caps.posedBounds)
            throw std::runtime_error("False animation capabilities");
        for (auto mode : {RenderMode::Hybrid, RenderMode::RayTracing})
        {
            bool failed = false;
            try
            {
                backend->SetRenderMode(mode);
            }
            catch (const std::exception&)
            {
                failed = true;
            }
            if (!failed || backend->GetRenderMode() != RenderMode::Raster)
                throw std::runtime_error("Animated ray mode accepted");
        }
        draw("preserved");
        instances.erase(instances.begin());
        backend->SetScene(sources, instances);
        bool erased = false;
        try
        {
            backend->scene.Animation("a");
        }
        catch (const std::invalid_argument&)
        {
            erased = true;
        }
        if (!erased || backend->scene.Animation("b").time != state.time)
            throw std::runtime_error("Removal state contract failed");
        backend->SetScene(
          {{"node", std::filesystem::path("tests/fixtures/scene-animation/Node.glb")}},
          {{"node-a", "node", world(-1.2f, 0, 0)}, {"node-b", "node", world(1.2f, 0, 0)}});
        if (backend->scene.Animations("node").size() != 2)
            throw std::runtime_error("Missing rigid node clips");
        auto           nodeBind  = draw("node-bind");
        auto           boxBefore = backend->scene.InstanceBounds("node-b");
        AnimationState nodeState;
        nodeState.playing = false;
        nodeState.time    = .5f;
        backend->SetSceneAnimation("node-b", nodeState);
        auto nodePose = draw("node-pose");
        auto boxAfter = backend->scene.InstanceBounds("node-b");
        if (
          Difference(nodeBind, nodePose) < .01 || boxAfter->minimum[0] <= boxBefore->minimum[0] + .1f
          || backend->scene.Animation("node-a").time != 0)
            throw std::runtime_error("Rigid node animation did not move geometry/bounds");
        auto floor = world(0, -1.4f, 0);
        floor[0] = floor[10] = 3;
        floor[5]             = .15f;
        std::vector<SceneInstance> shadowScene{{"b", "rig"}, {"floor", "box", floor, false}};
        backend->SetScene(sources, shadowScene);
        camera.pitch = 25;
        backend->SetCamera(camera);
        DirectionalLight light;
        light.direction = {-1, 2, 1};
        light.intensity = 8;
        backend->SetDirectionalLight(light);
        backend->SetLighting(LightingMode::PBR);
        SceneShadowSettings shadows;
        shadows.enabled  = true;
        shadows.softness = 0;
        backend->SetShadows(shadows);
        auto mask = [&](const std::string& name, bool seek) {
            shadowScene[0].castShadow = true;
            backend->SetScene(sources, shadowScene);
            if (seek)
                backend->SetSceneAnimation("b", {1, clips[1].duration * .4f, 1, false, true});
            auto on                   = draw(name);
            shadowScene[0].castShadow = false;
            backend->SetScene(sources, shadowScene);
            auto off = draw(name + "-off");
            for (size_t pixel = 0; pixel < on.pixels.size(); pixel++)
                on.pixels[pixel] = off.pixels[pixel] > on.pixels[pixel] + 8 ? 255 : 0;
            return on;
        };
        auto shadowBind = mask("shadow-bind", false);
        auto shadowPose = mask("shadow-pose", true);
        if (
          std::count(shadowPose.pixels.begin(), shadowPose.pixels.end(), 255) < 40
          || Difference(shadowBind, shadowPose) < .03)
            throw std::runtime_error("Animated shadow did not follow pose or cast flag");
        std::cout << "GPU pose delta left=" << left << " right=" << right
                  << "; independent states, clock, seek, sharing and preservation passed\n";
        backend.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout << api << " Scene animation GPU skinning, queries and paired state contract passed\n";
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
