#include "CameraData.hpp"
#include "ModelData.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace
{
    void require(bool ok, const char* message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }
    void close(float a, float b)
    {
        require(std::abs(a - b) < 1e-5f, "Camera matrix mismatch");
    }
    std::array<float, 3> project(const render::detail::SceneUniforms& s, std::array<float, 3> p)
    {
        float v[4] = {};
        for (int r = 0; r < 4; r++)
        {
            v[r] = s.viewProjection[12 + r];
            for (int k = 0; k < 3; k++)
                v[r] += s.viewProjection[k * 4 + r] * p[k];
        }
        return {v[0] / v[3], v[1] / v[3], v[2] / v[3]};
    }
} // namespace
int main(int argc, char** argv)
{
    try
    {
        render::Camera c;
        c.position       = {0, 0, 5};
        c.nearPlane      = 1;
        c.farPlane       = 10;
        c.verticalFov    = 90;
        const auto model = render::detail::EmptyModelData();
        auto       s     = render::detail::MakeSceneUniforms(*model, c, 2);
        close(project(s, {0, 0, 4})[2], 0);
        close(project(s, {0, 0, -5})[2], 1);
        close(project(s, {2, 0, 4})[0], 1);
        c.projection         = render::Projection::Orthographic;
        c.orthographicHeight = 4;
        s                    = render::detail::MakeSceneUniforms(*model, c, 2);
        close(project(s, {4, 2, 0})[0], 1);
        close(project(s, {4, 2, -2})[1], 1);
        require(
          s.cameraUp[3] == -2 && s.irradiance[1][3] == 1 && s.irradiance[2][3] == 10, "Camera ray metadata mismatch");
        c.orientation = {0, 0, float(std::sqrt(.5)), float(std::sqrt(.5))};
        s             = render::detail::MakeSceneUniforms(*model, c, 1);
        close(project(s, {1, 0, 0})[1], -.5f);
        c.orientation = {float(std::sqrt(.5)), 0, 0, float(std::sqrt(.5))};
        s             = render::detail::MakeSceneUniforms(*model, c, 1);
        for (float f : s.viewProjection)
            require(std::isfinite(f), "Vertical camera is singular");
        c.orientation = {0, 0, 0, 2};
        c             = render::detail::ValidateViewCamera(c);
        require(c.orientation[3] == 1, "Quaternion not normalized");
        auto bad        = c;
        bad.orientation = {0, 0, 0, 0};
        bool rejected   = false;
        try
        {
            render::detail::ValidateViewCamera(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Zero quaternion accepted");
        for (int i = 0; i < 5; i++)
        {
            bad = c;
            if (i == 0)
                bad.nearPlane = 0;
            if (i == 1)
                bad.farPlane = bad.nearPlane;
            if (i == 2)
                bad.verticalFov = 180;
            if (i == 3)
                bad.orthographicHeight = 0;
            if (i == 4)
                bad.position[0] = INFINITY;
            rejected = false;
            try
            {
                render::detail::ValidateViewCamera(bad);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            require(rejected, "Invalid lens accepted");
        }
        const render::Viewport v{90, 30, 100, 80};
        const auto             r = render::detail::ResolveViewport(v, 120, 70);
        require(r.x == 90 && r.y == 30 && r.width == 30 && r.height == 40, "Viewport clamp failed");
        require(render::detail::ResolveViewport(v, 20, 20).width == 0, "Outside viewport not empty");
        render::OrbitCamera orbit;
        orbit.yaw            = 37;
        orbit.pitch          = 25;
        orbit.target         = {1, 2, 3};
        const auto old       = render::detail::MakeSceneUniforms(*model, orbit, 1.4f),
                   converted = render::detail::MakeSceneUniforms(*model, render::CameraFromOrbit(orbit), 1.4f);
        for (int i = 0; i < 16; i++)
            close(old.viewProjection[i], converted.viewProjection[i]);
        if (argc > 1)
        {
            std::ofstream f(argv[1], std::ios::binary);
            for (int i = 0; i < 4; i++)
            {
                c                    = {};
                c.position           = {1, 2, 5};
                c.verticalFov        = 35 + i * 15;
                c.nearPlane          = .1f;
                c.farPlane           = 80;
                c.orthographicHeight = 4;
                c.projection         = i % 2 ? render::Projection::Orthographic : render::Projection::Perspective;
                c.orientation = i >= 2 ? std::array<float, 4>{.2f, .3f, .1f, .8f} : std::array<float, 4>{0, 0, 0, 1};
                const auto u  = render::detail::MakeSceneUniforms(*model, c, 1.5f);
                f.write(reinterpret_cast<const char*>(&u), sizeof(u));
            }
        }
        std::cout << "PASS camera: perspective/orthographic, clip planes, roll/poles, quaternion validation, viewport "
                     "and orbit conversion\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
