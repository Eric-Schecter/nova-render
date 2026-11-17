#pragma once
#include "ModelData.hpp"
#include "render/AmbientOcclusion.hpp"
#include "render/Renderer.hpp"
#include <algorithm>
#include <cstring>
namespace render::detail
{
    struct PathUniforms
    {
        SceneUniforms                          scene{};
        std::array<uint32_t, 4>                frame{};   // accumulated samples, width, height, bounce limit
        std::array<uint32_t, 4>                options{}; // samples per frame, environment face size
        std::array<float, 4>                   clear{};
        std::array<std::array<uint32_t, 4>, 5> images{}; // texel offset, width, height
        std::array<uint32_t, 4> lightInfo{};             // explicit collection, count, shadow master, world bias bits
        std::array<std::array<std::array<float, 4>, 4>, 16> lights{};
    };
    static_assert(sizeof(PathUniforms) == 1552, "HLSL path parameter layout");
    inline size_t PathScratchBytes(const PathUniforms& p)
    {
        if (p.options[3] & 64u)
            return size_t(p.frame[1]) * p.frame[2] * 32;
        if (p.options[3] & 32u)
        {
            uint32_t bootstrap;
            std::memcpy(&bootstrap, &p.clear[3], 4);
            const size_t pixels = size_t(p.frame[1]) * p.frame[2];
            return (14 * pixels + 16 + size_t(bootstrap) * 2 + std::min(size_t(1024), pixels) * 200) * 4;
        }
        return size_t(p.frame[1]) * p.frame[2] * 16
               + ((p.options[3] & 16u) ? (size_t(32768) + size_t(p.images[2][3]) * p.frame[3] * 16) * 4 : 0);
    }
    struct PathScene
    {
        const ModelData&                       model;
        const EnvironmentMip&                  sky;
        std::vector<unsigned char>             texels;
        std::array<std::array<uint32_t, 4>, 5> images{};
    };
    // Append to material texels: 8-word header, emitter CDF records, primitive PMFs, cubemap CDF.
    // images[0].w points at the header; bindings and the legacy 512-byte parameter prefix stay fixed.
    void PrepareLightSampling(PathScene&, bool hdr);
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
    void                        ValidateAmbientOcclusion(const AmbientOcclusionSettings&);
    void                        ValidatePathTracing(const PathTracingSettings&);
    class PathSession
    {
      public:
        // Private validation mode writes raw linear luminance instead of tone-mapped RGBA8.
        explicit PathSession(bool linearOutput = false)
          : linearOutput(linearOutput)
        {
        }
        RendererCapabilities Capabilities(GraphicsAPI api, const RendererConfig& config);
        const ModelImage&    Render(
             std::shared_ptr<const ModelData>       model,
             std::shared_ptr<const EnvironmentData> environment,
             const SceneUniforms&,
             const PathTracingSettings&,
             const std::array<float, 4>&     clear,
             uint32_t                        width,
             uint32_t                        height,
             const AmbientOcclusionSettings* ao      = nullptr,
             const std::vector<SceneLight>*  lights  = nullptr,
             const SceneShadowSettings&      shadows = {});
        void     Reset() { samples = 0; }
        uint32_t Samples() const { return samples; }

      private:
        std::unique_ptr<PathDevice>            device;
        RendererCapabilities                   capabilities;
        bool                                   queried      = false;
        bool                                   linearOutput = false;
        std::shared_ptr<const ModelData>       currentModel;
        std::shared_ptr<const EnvironmentData> currentEnvironment;
        std::array<std::array<uint32_t, 4>, 5> imageInfo{};
        PathUniforms                           previous{};
        ModelImage                             image;
        uint32_t                               samples = 0;
    };
} // namespace render::detail
