#pragma once
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace render
{
    enum class LightType
    {
        Directional,
        Point,
        Spot
    };
    struct SceneLight
    {
        std::string          id;
        LightType            type    = LightType::Directional;
        bool                 enabled = true;
        std::array<float, 3> color{1, 1, 1};
        float                intensity = 1;
        std::array<float, 3> position{0, 0, 0};
        // Directional: surface toward source. Spot: source outward, editor local -Z.
        std::array<float, 3> direction{0, 1, 0};
        float                range          = 0;                          // World units; zero means unlimited.
        float                innerConeAngle = .35f, outerConeAngle = .6f; // Radian half angles.
        bool                 castShadow = false;
        // Directional disk radius: softness * .006 radians; point/spot: softness * .006 world units.
        float shadowSoftness = 1;
    };
    struct LightingCapabilities
    {
        bool        directional = false, point = false, spot = false;
        unsigned    maxLights          = 0;
        bool        directionalShadows = false, pointShadows = false, spotShadows = false;
        bool        shadowRequiresScene = true;
        // Ray source subpaths / photon emission support point and spot; directional remains camera NEE.
        bool        collectionLightSubpaths = false, collectionPhotonEmission = false;
        std::vector<LightType> sourceLightTypes;
        std::string estimator;
        std::string reason;
    };
    inline std::vector<SceneLight> ValidateSceneLights(const std::vector<SceneLight>& input)
    {
        if (input.size() > 16)
            throw std::invalid_argument("Lights: maximum 16 entries");
        auto                  result = input;
        std::set<std::string> ids;
        for (auto& light : result)
        {
            const auto fail = [&](const char* field) {
                throw std::invalid_argument("Light '" + light.id + "': invalid " + field);
            };
            if (light.id.empty() || light.id.size() > 128 || !ids.insert(light.id).second)
                fail("id");
            if (static_cast<unsigned>(light.type) > 2)
                fail("type");
            for (float c : light.color)
                if (!std::isfinite(c) || c < 0 || c > 1)
                    fail("linear RGB color");
            if (!std::isfinite(light.intensity) || light.intensity < 0 || light.intensity > 100)
                fail("intensity");
            if (!std::isfinite(light.shadowSoftness) || light.shadowSoftness < 0 || light.shadowSoftness > 4)
                fail("shadowSoftness");
            if (light.type != LightType::Directional)
            {
                for (float v : light.position)
                    if (!std::isfinite(v))
                        fail("position");
                if (!std::isfinite(light.range) || light.range < 0)
                    fail("range");
            }
            if (light.type != LightType::Point)
            {
                for (float v : light.direction)
                    if (!std::isfinite(v))
                        fail("direction");
                const double length =
                  std::hypot(double(light.direction[0]), double(light.direction[1]), double(light.direction[2]));
                if (!std::isfinite(length) || length < 1e-6)
                    fail("direction");
                for (float& v : light.direction)
                    v = float(v / length);
            }
            if(light.type==LightType::Spot && (!std::isfinite(light.innerConeAngle)||!std::isfinite(light.outerConeAngle)||light.innerConeAngle<0||light.innerConeAngle>light.outerConeAngle||light.outerConeAngle>=1.5707963267948966))
                fail("cone angles");
        }
        return result;
    }
} // namespace render
