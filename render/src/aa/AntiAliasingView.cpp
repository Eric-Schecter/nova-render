#include "../../../assets/AntiAliasing/AreaTex.h"
#include "../../../assets/AntiAliasing/SearchTex.h"
#include "AADevice.hpp"
#include <algorithm>
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
        std::vector<detail::AAPass> passes;
        auto                        pass = [&](int program, int target) {
            detail::AAPass p;
            p.program  = program;
            p.target   = target;
            p.uniforms = u;
            passes.push_back(p);
            return &passes.back();
        };
        auto* baseline                         = pass(0, detail::AABaseline);
        baseline->uniforms[1]                  = {};
        pass(0, detail::AAColor)->multisampled = method == AAMethod::MSAA;
        int filtered                           = detail::AAColor;
        if (method == AAMethod::FXAA)
        {
            pass(2, detail::AAFiltered)->sources[0] = detail::AAColor;
            filtered                                = detail::AAFiltered;
        }
        if (method == AAMethod::SSAA)
        {
            pass(1, detail::AAFiltered)->sources[0] = detail::AAColor;
            filtered                                = detail::AAFiltered;
        }
        if (method == AAMethod::SMAA)
        {
            pass(3, detail::AAGamma)->sources[0] = detail::AAColor;
            pass(4, detail::AAEdges)->sources[0] = detail::AAGamma;
            auto* p                              = pass(5, detail::AAWeights);
            p->sources[1]                        = detail::AAEdges;
            p->sources[6]                        = detail::AAArea;
            p->sources[7]                        = detail::AASearch;
            p                                    = pass(6, detail::AAFiltered);
            p->sources[0]                        = detail::AAColor;
            p->sources[2]                        = detail::AAWeights;
            filtered                             = detail::AAFiltered;
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
        auto* display       = pass(8, detail::AAOutput);
        display->sources[0] = filtered;
        display->sources[3] = detail::AABaseline;
        s.image             = s.device->Draw(passes);
        s.previous          = s.camera;
        s.previousTime      = s.time;
        s.previousJitter    = jitter;
        s.frames++;
        return s.image;
    }
} // namespace render
