#pragma once
#include <cstdint>
namespace render
{
    enum class AOMethod
    {
        Disabled,
        SSAO,
        HBAO,
        MSAO,
        RTAO
    };
    enum class AOOutput
    {
        Composite,
        Occlusion,
        Depth,
        Normals
    };
    struct AmbientOcclusionSettings
    {
        bool     enabled = false;
        AOMethod method  = AOMethod::SSAO;
        AOOutput output  = AOOutput::Composite;
        float    radius = .08f, bias = .001f, strength = 1;
        uint32_t samples = 16, seed = 1;
        bool     denoise         = true;
        float    resolutionScale = .5f;
    };
} // namespace render
