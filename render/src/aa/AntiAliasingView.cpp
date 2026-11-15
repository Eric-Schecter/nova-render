#include "../../../assets/AntiAliasing/AreaTex.h"
#include "../../../assets/AntiAliasing/SearchTex.h"
#include "AADevice.hpp"
#include "PostProcessing.hpp"
#include "ReflectionProbe.hpp"
#include "ScreenSpaceReflections.hpp"
#include "render/ShadowView.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>
namespace render::detail
{
    AATextureData AALookup(bool area)
    {
        const auto*   bytes    = area ? areaTexBytes : searchTexBytes;
        const int     channels = area ? 2 : 1;
        AATextureData data{
          uint32_t(area ? AREATEX_WIDTH : SEARCHTEX_WIDTH), uint32_t(area ? AREATEX_HEIGHT : SEARCHTEX_HEIGHT), {}};
        data.pixels.resize(size_t(data.width) * data.height * 4);
        for (size_t i = 0; i < size_t(data.width) * data.height; i++)
        {
            for (int c = 0; c < channels; c++)
                data.pixels[i * 4 + c] = bytes[i * channels + c];
            data.pixels[i * 4 + 3] = 255;
        }
        return data;
    }
    float HalfToFloat(uint16_t h)
    {
        const float    sign     = (h & 0x8000) ? -1.f : 1.f;
        const unsigned exponent = (h >> 10) & 31, mantissa = h & 1023;
        if (exponent == 0)
            return sign * std::ldexp(float(mantissa), -24);
        if (exponent == 31)
            return mantissa ? NAN : sign * INFINITY;
        return sign * std::ldexp(float(mantissa + 1024), int(exponent) - 25);
    }
    Image AAReadImage(const uint16_t* values, uint32_t width, uint32_t height, size_t rowBytes)
    {
        Image image{width, height, {}};
        image.pixels.resize(size_t(width) * height * 4);
        for (uint32_t y = 0; y < height; y++)
            for (uint32_t x = 0; x < width; x++)
                for (int c = 0; c < 4; c++)
                {
                    const auto* row =
                      reinterpret_cast<const uint16_t*>(reinterpret_cast<const unsigned char*>(values) + y * rowBytes);
                    float value = HalfToFloat(row[x * 4 + c]);
                    if (!std::isfinite(value))
                        throw std::runtime_error("Non-finite AA output");
                    image.pixels[(size_t(y) * width + x) * 4 + c] =
                      static_cast<unsigned char>(std::clamp(value, 0.f, 1.f) * 255 + .5f);
                }
        return image;
    }
} // namespace render::detail
namespace render
{
    namespace
    {
        float Halton(uint32_t i, uint32_t base)
        {
            float f = 1, v = 0;
            while (i)
            {
                f /= base;
                v += f * (i % base);
                i /= base;
            }
            return v;
        }
    } // namespace
    struct AntiAliasingView::Impl
    {
        std::unique_ptr<detail::AADevice> device;
        AntiAliasingSettings              settings;
        OrbitCamera                       camera, previous;
        float                             time = 0, previousTime = 0;
        std::array<float, 2>              previousJitter{};
        uint32_t                          frames = 0, width = 0, height = 0;
        Image                             image;
        std::optional<ShadowSettings>     shadow;
        PostProcessingSettings            post;
        detail::PostProcessingState       postState;
        bool                              reflectionScene = false;
        ScreenSpaceReflectionSettings     reflections;
        bool                              planarScene = false;
        PlanarReflectionSettings          planar;
        bool                              probeScene = false, probeDirty = true;
        uint32_t                          probeCaptures = 0;
        ReflectionProbeSettings           probe;
        bool                              hdrScene       = false;
        float                             sceneIntensity = 1;
    };
    AntiAliasingView::AntiAliasingView(const RendererConfig& config)
      : impl_(std::make_unique<Impl>())
    {
        impl_->settings.method = AAMethod::TAA;
        impl_->settings.split  = true;
        impl_->camera.yaw      = 20;
        impl_->camera.pitch    = 15;
        impl_->camera.distance = 6.2f;
        switch (ResolveGraphicsAPI(config))
        {
#ifdef __APPLE__
        case GraphicsAPI::Metal: impl_->device = detail::CreateMetalAA(config); break;
#endif
#ifdef NOVA_HAS_VULKAN
        case GraphicsAPI::Vulkan: impl_->device = detail::CreateVulkanAA(config); break;
#endif
#ifdef _WIN32
        case GraphicsAPI::D3D12: impl_->device = detail::CreateD3D12AA(config); break;
#endif
        default: throw std::runtime_error("AA backend unavailable");
        }
    }
    AntiAliasingView::~AntiAliasingView() = default;
    void AntiAliasingView::SetAntiAliasing(const AntiAliasingSettings& s)
    {
        if (
          static_cast<unsigned>(s.method) > 5 || !std::isfinite(s.historyWeight) || s.historyWeight < 0
          || s.historyWeight > .98f || !std::isfinite(s.zoom) || s.zoom < 1 || s.zoom > 8)
            throw std::invalid_argument("Invalid AA settings");
        if (
          impl_->settings.method != s.method || impl_->settings.historyWeight != s.historyWeight
          || impl_->settings.textureFiltering != s.textureFiltering)
            ResetHistory();
        impl_->settings = s;
    }
    AntiAliasingSettings AntiAliasingView::GetAntiAliasing() const
    {
        return impl_->settings;
    }
    void AntiAliasingView::SetCamera(const OrbitCamera& c)
    {
        if (
          !std::isfinite(c.yaw) || !std::isfinite(c.pitch) || !std::isfinite(c.distance) || !std::isfinite(c.exposure)
          || c.distance < 4 || c.distance > 20 || std::abs(c.pitch) > 60 || c.exposure <= 0 || c.exposure > 4)
            throw std::invalid_argument("Invalid AA camera");
        if (
          std::abs(c.yaw - impl_->camera.yaw) > 20 || std::abs(c.distance - impl_->camera.distance) > 2
          || c.exposure != impl_->camera.exposure)
            ResetHistory();
        impl_->camera = c;
    }
    OrbitCamera AntiAliasingView::GetCamera() const
    {
        return impl_->camera;
    }
    void AntiAliasingView::SetTime(float t)
    {
        if (!std::isfinite(t) || t < 0 || t > 1e6)
            throw std::invalid_argument("Invalid AA time");
        if (std::abs(t - impl_->time) > .25f)
            ResetHistory();
        impl_->time = t;
    }
    void AntiAliasingView::ResetHistory()
    {
        impl_->frames         = 0;
        impl_->previousJitter = {};
    }
    uint32_t AntiAliasingView::GetHistoryFrames() const
    {
        return impl_->settings.method == AAMethod::TAA ? impl_->frames : 0;
    }
    void AntiAliasingView::ConfigureShadow(const ShadowSettings& value)
    {
        float length = 0;
        for (float c : value.lightDirection)
        {
            if (!std::isfinite(c))
                throw std::invalid_argument("Invalid light direction");
            length += c * c;
        }
        if (
          unsigned(value.method) > 3
          || (value.resolution != 512 && value.resolution != 1024 && value.resolution != 2048)
          || !std::isfinite(value.depthBias) || value.depthBias < 0 || value.depthBias > .02f
          || !std::isfinite(value.normalBias) || value.normalBias < 0 || value.normalBias > .1f
          || !std::isfinite(value.filterRadius) || value.filterRadius < 1 || value.filterRadius > 6
          || !std::isfinite(value.lightSize) || value.lightSize < .01f || value.lightSize > 1.5f || length < .0001f)
            throw std::invalid_argument("Invalid shadow settings");
        const auto old = impl_->shadow;
        impl_->shadow  = value;
        if (
          !old || old->method != value.method || old->resolution != value.resolution
          || old->depthBias != value.depthBias || old->normalBias != value.normalBias
          || old->filterRadius != value.filterRadius || old->lightSize != value.lightSize
          || old->lightDirection != value.lightDirection)
            ResetHistory();
    }
    void AntiAliasingView::SetPostProcessing(const PostProcessingSettings& v)
    {
        detail::ValidatePostProcessing(v);
        if (v.enabled != impl_->post.enabled || v.autoExposure != impl_->post.autoExposure)
            impl_->postState.Reset();
        impl_->post = v;
    }
    PostProcessingSettings AntiAliasingView::GetPostProcessing() const
    {
        return impl_->post;
    }
    void AntiAliasingView::ConfigureHDRScene()
    {
        impl_->hdrScene       = true;
        impl_->settings.split = false;
        impl_->camera         = {0, 0, 6.2f, 1};
        ResetHistory();
    }
    void AntiAliasingView::ConfigureSceneIntensity(float v)
    {
        if (!std::isfinite(v) || v < .05f || v > 20)
            throw std::invalid_argument("Invalid scene intensity");
        if (v != impl_->sceneIntensity)
            ResetHistory();
        impl_->sceneIntensity = v;
    }
    float AntiAliasingView::GetSceneIntensity() const
    {
        return impl_->sceneIntensity;
    }
    void AntiAliasingView::ConfigureReflectionScene()
    {
        impl_->reflectionScene = true;
        impl_->settings.split  = false;
        impl_->camera          = {18, 22, 8, 1};
        UseDefaultReflectionEnvironment();
        ResetHistory();
    }
    void AntiAliasingView::SetReflectionProbe(const ReflectionProbeSettings& value)
    {
        detail::ValidateProbe(value);
        const auto& old = impl_->probe;
        if (!impl_->probeScene || old.position != value.position)
            impl_->probeDirty = true;
        if (
          !impl_->probeScene || old.position != value.position || old.boxMin != value.boxMin
          || old.boxMax != value.boxMax || old.enabled != value.enabled || old.boxProjection != value.boxProjection
          || old.roughness != value.roughness || old.intensity != value.intensity
          || old.blendDistance != value.blendDistance || old.debug != value.debug)
            ResetHistory();
        impl_->probeScene = true;
        impl_->probe      = value;
    }
    ReflectionProbeSettings AntiAliasingView::GetReflectionProbe() const
    {
        return impl_->probe;
    }
    void AntiAliasingView::UpdateProbe()
    {
        impl_->probeDirty = true;
        ResetHistory();
    }
    uint32_t AntiAliasingView::GetProbeCaptureCount() const
    {
        return impl_->probeCaptures;
    }
    void AntiAliasingView::SetPlanarReflections(const PlanarReflectionSettings& s)
    {
        detail::ValidatePlanar(s);
        const auto& old = impl_->planar;
        if (
          !impl_->planarScene || old.floorEnabled != s.floorEnabled || old.mirrorEnabled != s.mirrorEnabled
          || old.floorRoughness != s.floorRoughness || old.mirrorRoughness != s.mirrorRoughness
          || old.floorIntensity != s.floorIntensity || old.mirrorIntensity != s.mirrorIntensity)
            ResetHistory();
        impl_->planarScene = true;
        impl_->planar      = s;
    }
    PlanarReflectionSettings AntiAliasingView::GetPlanarReflections() const
    {
        return impl_->planar;
    }
    void AntiAliasingView::SetScreenSpaceReflections(const ScreenSpaceReflectionSettings& s)
    {
        detail::ValidateReflections(s);
        const auto& o       = impl_->reflections;
        bool        changed = o.enabled != s.enabled || o.hierarchical != s.hierarchical || o.maxSteps != s.maxSteps
                       || o.maxDistance != s.maxDistance || o.thickness != s.thickness || o.stride != s.stride
                       || o.roughnessCutoff != s.roughnessCutoff || o.edgeFade != s.edgeFade
                       || o.intensity != s.intensity || o.environmentIntensity != s.environmentIntensity
                       || o.debug != s.debug || o.debugMip != s.debugMip;
        impl_->reflections = s;
        if (changed)
            ResetHistory();
    }
    ScreenSpaceReflectionSettings AntiAliasingView::GetScreenSpaceReflections() const
    {
        return impl_->reflections;
    }
    void AntiAliasingView::LoadReflectionEnvironment(const std::filesystem::path& path)
    {
        auto data  = detail::LoadEnvironmentFile(path);
        auto atlas = detail::ReflectionEnvironmentAtlas(data.get());
        impl_->device->SetReflectionEnvironment(atlas);
        impl_->probeDirty = true;
        ResetHistory();
    }
    void AntiAliasingView::UseDefaultReflectionEnvironment()
    {
        auto atlas = detail::ReflectionEnvironmentAtlas();
        impl_->device->SetReflectionEnvironment(atlas);
        impl_->probeDirty = true;
        ResetHistory();
    }
    const Image& AntiAliasingView::DrawFrame(uint32_t w, uint32_t h)
    {
        if (!w || !h || w > 2048 || h > 2048)
            throw std::invalid_argument("AA dimensions must be 1..2048");
        auto& s = *impl_;
        if (w != s.width || h != s.height)
        {
            s.width  = w;
            s.height = h;
            ResetHistory();
        }
        const auto method = s.settings.method;
        const bool taa    = method == AAMethod::TAA;
        s.device->Resize(w, h, method == AAMethod::SSAA);
        const uint32_t             phase = s.frames % 8 + 1;
        const std::array<float, 2> jitter =
          taa ? std::array<float, 2>{Halton(phase, 2) - .5f, Halton(phase, 3) - .5f} : std::array<float, 2>{};
        detail::AAUniforms u{};
        u[0]             = {float(w), float(h), s.time, float(method)};
        u[1]             = {jitter[0], jitter[1], s.previousJitter[0], s.previousJitter[1]};
        const auto  prev = s.frames ? s.previous : s.camera;
        const float rad  = .01745329252f;
        u[2]             = {s.camera.yaw * rad, prev.yaw * rad, s.camera.exposure, s.frames ? s.previousTime : s.time};
        u[3]    = {s.frames ? 1.f : 0.f, s.settings.historyWeight, s.settings.zoom, s.settings.split ? 1.f : 0.f};
        u[4]    = {s.camera.distance, prev.distance, s.camera.pitch * rad, prev.pitch * rad};
        u[5]    = {1, 1, 0, 0};
        u[6][1] = s.settings.textureFiltering ? 1.f : 0.f;
        u[7]    = {.012f, .02f, .035f, .1f};
        if (s.shadow)
        {
            const auto& sh = *s.shadow;
            u[8]           = {float(sh.method), float(sh.resolution), sh.depthBias, sh.normalBias};
            u[9]           = {sh.lightDirection[0], sh.lightDirection[1], sh.lightDirection[2], sh.lightSize};
            u[10]          = {sh.filterRadius, 0, float(6 + 19 * 36 + 3 * 16 * 24 * 6), 0};
            u[7]           = {.025f, .035f, .055f, 1};
        }
        if (s.hdrScene)
        {
            u[10][2] = float(6 + 19 * 36 + 3 * 16 * 24 * 6);
            u[14][2] = s.sceneIntensity;
            u[7]     = {.002f, .003f, .005f, 1};
        }
        if (s.reflectionScene)
        {
            detail::PackReflectionUniforms(u, s.reflections, w, h, method == AAMethod::SSAA);
            if (s.planarScene)
                detail::PackPlanarUniforms(u, s.planar);
            if (s.probeScene)
                detail::PackProbeUniforms(u, s.probe);
            u[7] = {.025f, .035f, .055f, 1};
        }
        if (s.post.enabled || s.hdrScene || s.reflectionScene)
            u[6][2] = 1;
        std::vector<detail::AAPass> passes;
        auto                        pass = [&](int program, int target) {
            detail::AAPass p;
            p.program  = program;
            p.target   = target;
            p.uniforms = u;
            passes.push_back(p);
            return &passes.back();
        };
        if (s.reflectionScene)
        {
            if (s.probeScene)
                detail::AppendProbePasses(passes, u, s.probeDirty, method == AAMethod::MSAA, taa, s.settings.split);
            else if (s.planarScene)
                detail::AppendPlanarPasses(passes, u, s.planar, method == AAMethod::MSAA, taa, s.settings.split);
            else
                detail::AppendReflectionPasses(
                  passes, u, s.reflections, method == AAMethod::MSAA, taa, s.settings.split);
        }
        else
        {
            const bool shadowed = s.shadow && s.shadow->method != ShadowMethod::Off;
            if (shadowed)
            {
                auto* p        = pass(9, detail::AAShadowMap);
                p->uniforms[7] = {1, 0, 0, 1};
            }
            if ((!s.shadow && !s.hdrScene && !s.reflectionScene) || s.settings.split)
            {
                auto* baseline        = pass(s.hdrScene ? 16 : s.shadow ? 10 : 0, detail::AABaseline);
                baseline->uniforms[1] = {};
                if (shadowed)
                    baseline->sources[0] = detail::AAShadowMap;
            }
            auto* color         = pass(s.hdrScene ? 16 : s.shadow ? 10 : 0, detail::AAColor);
            color->multisampled = method == AAMethod::MSAA;
            if (shadowed)
                color->sources[0] = detail::AAShadowMap;
        }
        int filtered = detail::AAColor;
        if (method == AAMethod::SSAA && !s.reflectionScene)
        {
            pass(1, detail::AAFiltered)->sources[0] = detail::AAColor;
            filtered                                = detail::AAFiltered;
        }
        if (taa)
        {
            filtered                                  = s.frames % 2 ? detail::AAHistoryB : detail::AAHistoryA;
            auto* p                                   = pass(7, filtered);
            p->sources[0]                             = detail::AAColor;
            p->sources[3]                             = s.frames % 2 ? detail::AAHistoryA : detail::AAHistoryB;
            p->sources[4]                             = detail::AAMotion;
            p->sources[5]                             = detail::AAPrevMotion;
            pass(1, detail::AAPrevMotion)->sources[0] = detail::AAMotion;
        }
        const int baselineSource =
          ((!s.shadow && !s.hdrScene && !s.reflectionScene) || s.settings.split) ? detail::AABaseline : detail::AADummy;
        auto effectivePost = s.post;
        if (
          (s.reflectionScene && unsigned(s.reflections.debug) >= 2)
          || (s.probeScene && s.probe.debug == ProbeDebug::Influence))
            effectivePost.enabled = false;
        const auto post =
          detail::AppendPostProcessing(passes, filtered, baselineSource, w, h, effectivePost, s.postState, u);
        filtered = post.color;
        if (method == AAMethod::FXAA)
        {
            pass(2, detail::AAFiltered)->sources[0] = filtered;
            filtered                                = detail::AAFiltered;
        }
        if (method == AAMethod::SMAA)
        {
            pass(3, detail::AAGamma)->sources[0] = filtered;
            pass(4, detail::AAEdges)->sources[0] = detail::AAGamma;
            auto* p                              = pass(5, detail::AAWeights);
            p->sources[1]                        = detail::AAEdges;
            p->sources[6]                        = detail::AAArea;
            p->sources[7]                        = detail::AASearch;
            p                                    = pass(6, detail::AAFiltered);
            p->sources[0]                        = filtered;
            p->sources[2]                        = detail::AAWeights;
            filtered                             = detail::AAFiltered;
        }
        auto* display       = pass(8, detail::AAOutput);
        display->sources[0] = filtered;
        if ((!s.shadow && !s.hdrScene && !s.reflectionScene) || s.settings.split)
            display->sources[3] = post.baseline;
        s.image = s.device->Draw(passes);
        if (s.probeScene && s.probeDirty)
        {
            s.probeDirty = false;
            s.probeCaptures++;
        }
        s.previous       = s.camera;
        s.previousTime   = s.time;
        s.previousJitter = jitter;
        s.frames++;
        return s.image;
    }
} // namespace render
