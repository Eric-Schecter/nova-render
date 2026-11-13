#pragma once
#include "ModelData.hpp"
#include "render/Renderer.hpp"
namespace render::detail
{
    struct PathUniforms
    {
        SceneUniforms                          scene{};
        std::array<uint32_t, 4>                frame{};   // accumulated samples, width, height, bounce limit
        std::array<uint32_t, 4>                options{}; // samples per frame, environment face size
        std::array<float, 4>                   clear{};
        std::array<std::array<uint32_t, 4>, 5> images{}; // texel offset, width, height
    };
    static_assert(sizeof(PathUniforms) == 512, "HLSL path parameter layout");
    struct PathScene
    {
        const ModelData&                       model;
        const EnvironmentMip&                  sky;
        std::vector<unsigned char>             texels;
        std::array<std::array<uint32_t, 4>, 5> images{};
    };
    class PathDevice
    {
      public:
        virtual ~PathDevice() = default;
        RendererCapabilities               capabilities;
        virtual void                       SetScene(const PathScene&) = 0;
        virtual std::vector<unsigned char> Trace(const PathUniforms&) = 0;
    };
    std::unique_ptr<PathDevice> CreateMetalPathDevice(const RendererConfig&);
    std::unique_ptr<PathDevice> CreateVulkanPathDevice(const RendererConfig&);
    std::unique_ptr<PathDevice> CreateD3D12PathDevice(const RendererConfig&);
    void                        ValidatePathTracing(const PathTracingSettings&);
    class PathSession
    {
      public:
        RendererCapabilities Capabilities(GraphicsAPI api, const RendererConfig& config);
        const ModelImage&    Render(
             std::shared_ptr<const ModelData>       model,
             std::shared_ptr<const EnvironmentData> environment,
             const SceneUniforms&,
             const PathTracingSettings&,
             const std::array<float, 4>& clear,
             uint32_t                    width,
             uint32_t                    height);
        void     Reset() { samples = 0; }
        uint32_t Samples() const { return samples; }

      private:
        std::unique_ptr<PathDevice>            device;
        RendererCapabilities                   capabilities;
        bool                                   queried = false;
        std::shared_ptr<const ModelData>       currentModel;
        std::shared_ptr<const EnvironmentData> currentEnvironment;
        std::array<std::array<uint32_t, 4>, 5> imageInfo{};
        PathUniforms                           previous{};
        ModelImage                             image;
        uint32_t                               samples = 0;
    };
} // namespace render::detail
