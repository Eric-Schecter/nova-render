#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <render/RayTracingView.hpp>
namespace example
{
    struct ComparisonProgress
    {
        render::RayIntegrator integrator;
        uint32_t              samples      = 0;
        double                milliseconds = 0;
        bool                  complete     = false;
    };
    struct ComparisonBudget
    {
        uint32_t samples = 64, milliseconds = 0;
    };
    // Example policy: four independent views, synchronized scene/camera, budgets and layout.
    class ComparisonScene
    {
      public:
        explicit ComparisonScene(const render::RendererConfig& config = {})
        {
            for (size_t i = 0; i < 4; ++i)
            {
                views[i] = std::make_unique<render::RayTracingView>(config);
                render::DirectionalLight light;
                light.intensity = 0;
                views[i]->SetDirectionalLight(light);
                render::EnvironmentSettings environment;
                environment.intensity           = 0;
                environment.backgroundIntensity = 0;
                environment.showBackground      = false;
                views[i]->SetEnvironment(environment);
            }
            Reset();
        }
        bool Supported() const { return views[0]->GetCapabilities().rayTracing; }
        void LoadModel(const std::filesystem::path& path)
        {
            for (auto& v : views)
                v->LoadModel(path);
            Reset();
        }
        void SetCamera(const render::OrbitCamera& value)
        {
            if (
              camera.yaw == value.yaw && camera.pitch == value.pitch && camera.distance == value.distance
              && camera.target == value.target && camera.exposure == value.exposure)
                return;
            for (auto& v : views)
                v->SetCamera(value);
            camera = value;
            Reset();
        }
        void SetSettings(render::RayTracingSettings value)
        {
            value.samplesPerFrame = 1;
            value.denoise         = false;
            value.lightSampling   = true;
            for (size_t i = 0; i < 4; ++i)
            {
                value.integrator = static_cast<render::RayIntegrator>(i);
                views[i]->SetRayTracing(value);
            }
            Reset();
        }
        void SetBudget(ComparisonBudget value)
        {
            if (!value.samples || value.samples > 65536 || value.milliseconds > 60000)
                throw std::invalid_argument("Invalid comparison budget");
            budget = value;
            Reset();
        }
        void Reset()
        {
            for (size_t i = 0; i < 4; ++i)
            {
                views[i]->ResetAccumulation();
                progress[i] = {static_cast<render::RayIntegrator>(i), 0, 0, false};
            }
        }
        const std::array<ComparisonProgress, 4>& Progress() const { return progress; }
        const render::Image&                     DrawFrame(uint32_t tileSize)
        {
            if (tileSize != previousSize)
            {
                previousSize = tileSize;
                Reset();
            }
            for (size_t i = 0; i < 4; ++i)
                if (!progress[i].complete)
                {
                    const auto start = std::chrono::steady_clock::now();
                    frames[i]        = views[i]->DrawFrame(tileSize, tileSize);
                    auto& p          = progress[i];
                    p.milliseconds +=
                      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    p.samples = views[i]->GetAccumulatedSamples();
                    p.complete =
                      budget.milliseconds ? p.milliseconds >= budget.milliseconds : p.samples >= budget.samples;
                }
            const auto side = frames[0].width;
            image.width = image.height = side * 2;
            image.pixels.resize(size_t(image.width) * image.height * 4);
            for (size_t i = 0; i < 4; ++i)
                for (uint32_t y = 0; y < side; ++y)
                    std::memcpy(
                      image.pixels.data() + ((size_t(y + (i / 2) * side) * image.width) + (i % 2) * side) * 4,
                      frames[i].pixels.data() + size_t(y) * side * 4,
                      size_t(side) * 4);
            return image;
        }

      private:
        std::array<std::unique_ptr<render::RayTracingView>, 4> views;
        std::array<ComparisonProgress, 4>                      progress;
        std::array<render::Image, 4>                           frames;
        render::Image                                          image;
        render::OrbitCamera                                    camera;
        ComparisonBudget                                       budget;
        uint32_t                                               previousSize = 0;
    };
} // namespace example
