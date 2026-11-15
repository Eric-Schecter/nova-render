#pragma once
namespace render
{
    enum class AAMethod
    {
        Off,
        FXAA,
        SMAA,
        MSAA,
        SSAA,
        TAA
    };
    struct AntiAliasingSettings
    {
        AAMethod method           = AAMethod::Off;
        float    historyWeight    = .9f;
        float    zoom             = 1;
        bool     split            = false;
        bool     textureFiltering = true;
    };
} // namespace render
