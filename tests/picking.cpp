#include "SceneData.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
using Json = nlohmann::json;
namespace
{
    void require(bool value, const std::string& message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
    void close(double value, double expected)
    {
        require(
          std::abs(value - expected) <= 1e-5 * std::max(1e-6, std::abs(expected)),
          "Picking numeric mismatch: " + std::to_string(value) + " vs " + std::to_string(expected));
    }
    Json result(const std::optional<render::SceneHit>& h)
    {
        if (!h)
            return nullptr;
        return Json{{"id", h->id}, {"distance", h->distance}, {"position", h->position}, {"normal", h->normal}};
    }
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
            throw std::invalid_argument("Expected assets, fixtures and output paths");
        Json fixtures;
        std::ifstream(argv[2]) >> fixtures;
        Json                        results = Json::array();
        render::detail::SceneData   scene;
        const std::filesystem::path file = std::filesystem::path(argv[1]) / "SceneSample/Red.glb";
        for (const auto& test : fixtures)
        {
            std::vector<render::SceneInstance> instances;
            for (const auto& i : test["instances"])
                instances.push_back(
                  {i["id"].get<std::string>(), i["assetId"].get<std::string>(), i["world"].get<render::Matrix4>()});
            scene.Update({{"box", file, test["sourceSpace"].get<bool>()}}, instances);
            const auto& r = test["ray"];
            render::Ray ray;
            ray.origin    = r["origin"].get<std::array<float, 3>>();
            ray.direction = r["direction"].get<std::array<float, 3>>();
            if (r.contains("includeIds"))
                ray.includeIds = r["includeIds"].get<std::vector<std::string>>();
            if (r.contains("excludeIds"))
                ray.excludeIds = r["excludeIds"].get<std::vector<std::string>>();
            std::optional<render::SceneHit> h;
            bool                            rejected = false;
            try
            {
                h = scene.Pick(ray);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            require(rejected == test["invalid"].get<bool>(), test["name"].get<std::string>() + ": validation mismatch");
            if (rejected)
            {
                results.push_back("invalid");
                continue;
            }
            const auto& expected = test["expected"];
            require(bool(h) == !expected.is_null(), test["name"].get<std::string>() + ": hit/miss mismatch");
            if (h)
            {
                require(h->id == expected["id"].get<std::string>(), "Wrong instance ID");
                close(h->distance, expected["distance"].get<double>());
                for (int k = 0; k < 3; k++)
                {
                    close(h->position[k], expected["position"][k].get<double>());
                    close(h->normal[k], expected["normal"][k].get<double>());
                }
                close(std::hypot(h->normal[0], h->normal[1], h->normal[2]), 1);
            }
            results.push_back(result(h));
        }
        // Invalid queries are rejected even for empty/inactive scenes.
        scene.active = false;
        render::Ray bad;
        bad.direction = {0, 0, 0};
        bool rejected = false;
        try
        {
            scene.Pick(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Inactive scene skipped validation");
        bad           = {};
        bad.origin[0] = std::numeric_limits<float>::quiet_NaN();
        rejected      = false;
        try
        {
            scene.Pick(bad);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "NaN accepted");
        // Large BVH plus triangle-exact rejection inside bounding boxes.
        auto data = std::make_shared<render::detail::ModelData>();
        constexpr int triangleCount = 32768;
        for (int i = 0; i < triangleCount; i++)
            for (auto p : std::array<std::array<float, 3>, 3>{
                   {{float(i * 3), 0, 0}, {float(i * 3 + 1), 0, 0}, {float(i * 3), 1, 0}}})
            {
                render::detail::ModelVertex v{};
                std::copy(p.begin(), p.end(), v.position);
                v.position[3] = 1;
                data->vertices.push_back(v);
            }
        const auto               begin = std::chrono::steady_clock::now();
        render::detail::PickMesh mesh(data, true);
        for (int i = 0; i < triangleCount; i += 137)
        {
            const auto h = mesh.Pick({double(i * 3) + .2, .2, 5}, {0, 0, -1}, 100);
            require(h && h->triangle == uint32_t(i) && h->distance == 5, "BVH hit mismatch");
            require(!mesh.Pick({double(i * 3) + .9, .9, 5}, {0, 0, -1}, 100), "Picked AABB instead of triangle");
        }
        std::ofstream(argv[3]) << results.dump(2) << '\n';
        std::cout << "PASS " << fixtures.size() << " shared ray/normal/filter cases and " << triangleCount
                  << "-triangle scene BVH ("
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count()
                  << " ms)\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
