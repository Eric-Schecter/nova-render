#pragma once
namespace render
{
    enum class RasterPipeline
    {
        Forward,
        Deferred,
        ForwardPlus,
        ClusteredForward
    };
    enum class GBufferView
    {
        Final,
        BaseColor,
        WorldNormal,
        GeometricNormal,
        LinearDepth,
        Roughness,
        Metallic,
        Occlusion,
        Emissive,
        WorldPosition
    };
    struct RasterSettings
    {
        RasterPipeline pipeline   = RasterPipeline::Forward;
        GBufferView    view       = GBufferView::Final;
        float          debugRange = 20;
    };
    struct RasterCapabilities
    {
        bool     forward = true, deferred = true, gBuffer = true;
        bool     forwardPlus = false, clusteredForward = false, deferredMSAA = false;
        bool     opaqueOnly = false, debugBypassesAAAndPost = true;
        bool     deferredRequiresModel = true;
        unsigned gBufferTargets = 5, gBufferBytesPerPixel = 40;
    };
} // namespace render
