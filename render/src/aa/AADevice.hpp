#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <render/AntiAliasingView.hpp>
#include <stdexcept>
namespace render::detail
{
    enum AAResource
    {
        AAColor,
        AAMotion,
        AAEdges,
        AAWeights,
        AAHistoryA,
        AAHistoryB,
        AAPrevMotion,
        AAFiltered,
        AAOutput,
        AABaseline,
        AABaselineMotion,
        AAGamma,
        AAArea,
        AASearch,
        AAMultiColor,
        AAMultiMotion,
        AADummy,
        AARawColor,
        AARawMotion,
        AARawMultiColor,
        AARawMultiMotion,
        AARawBaseline,
        AAShadowMap,
        AABloomDown0       = 23,
        AABloomUp0         = 28,
        AALuminance0       = 32,
        AAExposureA        = 38,
        AAExposureB        = 39,
        AAPostColor        = 40,
        AAPostBaseline     = 41,
        AASsrColor         = 42,
        AASsrMotion        = 43,
        AASsrNormal        = 44,
        AASsrDepth         = 45,
        AASsrMulti0        = 46,
        AASsrHiZ0          = 50,
        AASsrHit           = 62,
        AASsrComposite     = 63,
        AASsrEnvironment   = 64,
        AAPlanarFloor      = 65,
        AAPlanarMirror     = 69,
        AAProbeCubemap     = 73,
        AAProbeData        = 77,
        AAPipelineAlbedo   = 78,
        AAPipelineNormal   = 79,
        AAPipelinePosition = 80,
        AAPipelineMaterial = 81,
        AAPipelineDepth    = 82,
        AAPipelineTiles    = 83,
        AAPipelineLists    = 84,
        AAModelBase        = 85,
        AAModelNormal,
        AAModelPosition,
        AAModelEmission,
        AAOitAccum = 89,
        AAOitReveal,
        AARawOpaque,
        AAHybridShadow0 = 92,
        AAHybridShadow1,
        AAHybridShadow2,
        AAHybridShadow3,
        AAHybridPreviousNormal = 96,
        AAHybridPreviousDepth,
        AAHybridCurrentNormal,
        AAHybridCurrentDepth,
        AAHybridOitAccum,
        AAHybridOitReveal,
        AAModelClearcoat   = 102,
        AAHybridReflection = 103,
        AAReflectionHistoryA,
        AAReflectionHistoryB,
        AAReflectionPreviousGuide,
        AAShadowHistoryA,
        AAShadowHistoryB      = AAShadowHistoryA + 4,
        AAShadowPreviousDepth = AAShadowHistoryB + 4,
        AAShadowPreviousNormal,
        AAShadowPreviousMotion,
        AAHybridAO,
        AAAOHistoryA,
        AAAOHistoryB,
        AAAOPreviousGuide,
        AAAOCombinedPosition,
        AAResourceCount
    };
    using AAUniforms = std::array<std::array<float, 4>, 16>;
    struct AAPass
    {
        int                 program = 0, target = 0;
        std::array<int, 16> sources{};
        AAPass() { sources.fill(AADummy); }
        AAUniforms uniforms{};
        bool       multisampled = false;
    };

    struct AAExternalScene
    {
        void*                                                        instance    = nullptr;
        void*                                                        physical    = nullptr;
        void*                                                        device      = nullptr;
        uint32_t                                                     queueFamily = 0;
        std::function<void(void*)>                                   beforeScene;
        std::function<void(const AAPass&, void*, void*)>             draw;
        std::function<void(void*, const std::array<void*, 10>&, int)> bindGBuffer;
    };
    struct AASpec
    {
        uint32_t width, height, samples;
    };
    inline AASpec AAResourceSpec(
      int id, uint32_t w, uint32_t h, bool ss, bool external, uint32_t shadowSize = 1024, bool reflection = false)
    {
        if (id >= AAHybridAO && id < AAAOCombinedPosition)
            return {(w * (ss ? 2u : 1u) + 1) / 2 + 2, (h * (ss ? 2u : 1u) + 1) / 2 + 2, 1};
        if (id >= AAShadowHistoryA)
            return {w * (ss ? 2u : 1u) + 2, h * (ss ? 2u : 1u) + 2, 1};
        if (id >= AAHybridReflection)
            return {(w * (ss ? 2u : 1u) + 1) / 2 + 2, (h * (ss ? 2u : 1u) + 1) / 2 + 2, 1};
        if (id >= AAHybridPreviousNormal && id != AAModelClearcoat)
            return {w, h, 1};
        if (id >= AAModelBase)
            return {w * (ss ? 2u : 1u) + 2, h * (ss ? 2u : 1u) + 2, 1};
        if (id == AAPipelineLists)
            return {17, 16 * 9 * 16, 1};
        if (id == AAPipelineTiles)
            return {16, 9, 1};
        if (id >= AAPipelineAlbedo)
            return {w, h, 1};
        if (id == AAProbeData)
            return {20, 1, 1};
        if (id >= AAProbeCubemap && id < AAProbeData)
            return {384, 512, 1};
        if (id >= AAPlanarFloor)
            return {w * (ss ? 2 : 1), h * (ss ? 2 : 1), 1};
        if (id == AASsrEnvironment)
            return {256, 256, 1};
        if (id >= AASsrHiZ0 && id < AASsrHiZ0 + 12)
        {
            uint32_t d = 2u << (id - AASsrHiZ0), scale = ss ? 2 : 1;
            return {(w * scale + d - 1) / d, (h * scale + d - 1) / d, 1};
        }
        if ((id >= AASsrColor && id < AASsrHiZ0) || id == AASsrHit || id == AASsrComposite)
            return {w * (ss ? 2 : 1), h * (ss ? 2 : 1), id >= AASsrMulti0 && id < AASsrMulti0 + 4 ? 4u : 1u};
        if (reflection && (id == AAColor || id == AAMotion))
            return {w, h, 1};
        if (id >= AABloomDown0 && id < AABloomDown0 + 5)
        {
            uint32_t d = 2u << (id - AABloomDown0);
            return {(w + d - 1) / d, (h + d - 1) / d, 1};
        }
        if (id >= AABloomUp0 && id < AABloomUp0 + 4)
        {
            uint32_t d = 2u << (id - AABloomUp0);
            return {(w + d - 1) / d, (h + d - 1) / d, 1};
        }
        if (id >= AALuminance0 && id < AALuminance0 + 6)
        {
            uint32_t d = 1u << (2 * (id - AALuminance0 + 1));
            return {(w + d - 1) / d, (h + d - 1) / d, 1};
        }
        if (id == AAExposureA || id == AAExposureB)
            return {1, 1, 1};
        if (id == AAShadowMap)
            return {shadowSize, shadowSize, 1};
        if (id == AADummy)
            return {1, 1, 1};
        bool big = external ? (id == AARawColor || id == AARawMotion || id == AARawMultiColor || id == AARawMultiMotion)
                            : (id == AAColor || id == AAMotion);
        uint32_t pad = id >= AARawColor && id <= AARawBaseline ? 2 : 0;
        return {
          w * (ss && big ? 2 : 1) + pad,
          h * (ss && big ? 2 : 1) + pad,
          (id == AAMultiColor || id == AAMultiMotion || id == AARawMultiColor || id == AARawMultiMotion) ? 4u : 1u};
    }
    inline std::array<bool, AAResourceCount> AAUsed(const std::vector<AAPass>& passes)
    {
        std::array<bool, AAResourceCount> used{};
        used[AADummy] = true;
        for (const auto& p : passes)
        {
            used[p.target] = true;
            for (int source : p.sources)
                used[source] = true;
            if (p.program == 17 || p.program == 22 || p.program == -4 || p.program == -7)
            {
                for (int i = 1; i < 4; i++)
                    used[p.target + i] = true;
            }
            if (p.program == -4)
                used[AAModelClearcoat] = true;
            if (p.program == 0 || p.program == 10 || p.program == 16 || p.program == -6)
                used[p.target + 1] = true;
            if (p.multisampled)
            {
                if (p.program == 17)
                {
                    for (int i = 0; i < 4; i++)
                        used[AASsrMulti0 + i] = true;
                }
                else if (p.program < 0)
                    used[AARawMultiColor] = true;
                else
                    used[AAMultiColor] = used[AAMultiMotion] = true;
            }
        }
        return used;
    }
    struct AATextureData
    {
        uint32_t                   width, height;
        std::vector<unsigned char> pixels;
        bool                       hdr = false;
    };
    AATextureData AALookup(bool area);
    float         HalfToFloat(uint16_t value);
    Image         AAReadImage(const uint16_t* values, uint32_t width, uint32_t height, size_t rowBytes);
    class AADevice
    {
      public:
        virtual ~AADevice() = default;
        virtual void SetReflectionEnvironment(const AATextureData&, int = AASsrEnvironment)
        {
            throw std::runtime_error("Reflection environment unsupported");
        }
        virtual void Resize(uint32_t width, uint32_t height, bool supersampled) = 0;
        // GPU output belongs to this device and is valid until the next resize/draw.
        virtual void* GPUOutput() const { return nullptr; }
        virtual Image Capture() { throw std::runtime_error("GPU capture unsupported"); }
        uint64_t      readbackCount                                                 = 0;
        virtual Image Draw(const std::vector<AAPass>& passes, bool readback = true) = 0;
    };
    std::unique_ptr<AADevice> CreateMetalAA(const RendererConfig&, const AAExternalScene& = {});
    std::unique_ptr<AADevice> CreateVulkanAA(const RendererConfig&, const AAExternalScene& = {});
    std::unique_ptr<AADevice> CreateD3D12AA(const RendererConfig&, const AAExternalScene& = {});
} // namespace render::detail
