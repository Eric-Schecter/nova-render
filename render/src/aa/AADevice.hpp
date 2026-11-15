#pragma once
#include <array>
#include <cmath>
#include <cstring>
#include <render/AntiAliasingView.hpp>
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
        AAResourceCount
    };
    using AAUniforms = std::array<std::array<float, 4>, 5>;
    struct AAPass
    {
        int                program = 0, target = 0;
        std::array<int, 8> sources{AADummy, AADummy, AADummy, AADummy, AADummy, AADummy, AADummy, AADummy};
        AAUniforms         uniforms{};
        bool               multisampled = false;
    };
    struct AATextureData
    {
        uint32_t                   width, height;
        std::vector<unsigned char> pixels;
    };
    AATextureData AALookup(bool area);
    float         HalfToFloat(uint16_t value);
    Image         AAReadImage(const uint16_t* values, uint32_t width, uint32_t height, size_t rowBytes);
    class AADevice
    {
      public:
        virtual ~AADevice()                                                      = default;
        virtual void  Resize(uint32_t width, uint32_t height, bool supersampled) = 0;
        virtual Image Draw(const std::vector<AAPass>& passes)                    = 0;
    };
    std::unique_ptr<AADevice> CreateMetalAA(const RendererConfig&);
    std::unique_ptr<AADevice> CreateVulkanAA(const RendererConfig&);
    std::unique_ptr<AADevice> CreateD3D12AA(const RendererConfig&);
} // namespace render::detail
