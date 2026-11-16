#include "CameraData.hpp"
#include "ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
namespace render
{
    Camera CameraFromOrbit(const OrbitCamera& orbit)
    {
        detail::ValidateCamera(orbit);
        const float     yaw = glm::radians(orbit.yaw), pitch = glm::radians(orbit.pitch);
        const glm::vec3 backward(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
        const glm::vec3 right(std::cos(yaw), 0, -std::sin(yaw)), up = glm::cross(backward, right);
        const auto      q = glm::quat_cast(glm::mat3(right, up, backward));
        Camera          result;
        for (int i = 0; i < 3; i++)
            result.position[i] = orbit.target[i] + backward[i] * orbit.distance;
        result.orientation = {q.x, q.y, q.z, q.w};
        result.exposure    = orbit.exposure;
        return result;
    }
} // namespace render
namespace render::detail
{
    bool SameCamera(const Camera& a, const Camera& b)
    {
        return a.position == b.position && a.orientation == b.orientation && a.projection == b.projection
               && a.verticalFov == b.verticalFov && a.orthographicHeight == b.orthographicHeight
               && a.nearPlane == b.nearPlane && a.farPlane == b.farPlane && a.exposure == b.exposure;
    }
    bool SameViewport(const std::optional<Viewport>& a, const std::optional<Viewport>& b)
    {
        return (!a && !b) || (a && b && a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height);
    }
    Camera ValidateViewCamera(const Camera& value)
    {
        auto c = value;
        for (float f : c.position)
            if (!std::isfinite(f))
                throw std::invalid_argument("Camera position must be finite");
        double length = 0;
        for (float f : c.orientation)
        {
            if (!std::isfinite(f))
                throw std::invalid_argument("Camera orientation must be finite");
            length = std::hypot(length, double(f));
        }
        if (length < 1e-8)
            throw std::invalid_argument("Camera orientation must be a nonzero quaternion");
        for (auto& f : c.orientation)
            f = float(f / length);
        if (
          (c.projection != Projection::Perspective && c.projection != Projection::Orthographic)
          || !std::isfinite(c.verticalFov) || c.verticalFov < 1 || c.verticalFov > 179
          || !std::isfinite(c.orthographicHeight) || c.orthographicHeight < 1e-5f || !std::isfinite(c.nearPlane)
          || !std::isfinite(c.farPlane) || c.nearPlane <= 0 || c.farPlane <= c.nearPlane || !std::isfinite(c.exposure)
          || c.exposure <= 0 || c.exposure > 16)
            throw std::invalid_argument(
              "Invalid camera lens: FOV 1..179, height >= 1e-5, 0 < near < far, exposure 0..16");
        return c;
    }
    void ValidateViewport(const std::optional<Viewport>& viewport)
    {
        if (viewport && (!viewport->width || !viewport->height))
            throw std::invalid_argument("Viewport dimensions must be positive");
    }
    Viewport ResolveViewport(
      const std::optional<Viewport>& viewport, uint32_t width, uint32_t height, uint32_t topInset)
    {
        const auto v =
          viewport.value_or(Viewport{0, std::min(topInset, height), width, height - std::min(topInset, height)});
        const auto x = std::min(v.x, width), y = std::min(v.y, height);
        return {x, y, std::min(v.width, width - x), std::min(v.height, height - y)};
    }
} // namespace render::detail
