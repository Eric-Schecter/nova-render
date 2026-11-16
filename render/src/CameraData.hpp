#pragma once
#include "render/Camera.hpp"
#include <optional>
namespace render::detail
{
    bool     SameCamera(const Camera& a, const Camera& b);
    bool     SameViewport(const std::optional<Viewport>& a, const std::optional<Viewport>& b);
    Camera   ValidateViewCamera(const Camera& camera);
    void     ValidateViewport(const std::optional<Viewport>& viewport);
    Viewport ResolveViewport(
      const std::optional<Viewport>& viewport, uint32_t width, uint32_t height, uint32_t topInset = 0);
} // namespace render::detail
