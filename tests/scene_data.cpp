#include "SceneData.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            throw std::invalid_argument("Expected assets directory");
        const std::filesystem::path     root = argv[1];
        std::vector<render::SceneAsset> assets{
          {"red", root / "SceneSample/Red.glb"}, {"blue", root / "SceneSample/Blue.glb"}};
        std::vector<render::SceneInstance> instances{{"one", "red"}, {"two", "red"}, {"three", "blue"}};
        render::detail::SceneData          scene;
        scene.Update(assets, instances);
        Require(scene.instances.size() == 3 && scene.draws.size() == 2, "Missing instances/groups");
        Require(scene.model->vertices.size() == 72, "Geometry duplicated per instance");
        const auto model  = scene.model;
        const auto bounds = scene.AssetBounds("red");
        Require(bounds && bounds->minimum[0] == -1 && bounds->maximum[1] == .5f, "Wrong normalized bounds");
        instances[1].world[12] = 3;
        scene.Update(assets, instances);
        Require(scene.model == model, "Transform-only update repacked resources");
        assets[0].sourceSpace = true;
        scene.Update(assets, instances);
        Require(scene.model == model, "Space-only update repacked resources");
        const auto source = scene.AssetBounds("red");
        Require(
          source && source->minimum[0] == 0 && source->maximum[0] == 4 && source->maximum[1] == 2,
          "Source units or node transforms lost");
        // Draw groups are ordered by asset ID; verify the red group's independent transforms.
        const auto& red = scene.draws[1];
        Require(red.instanceCount == 2 && red.firstInstance == 1, "Wrong shared draw range");
        Require(
          scene.instances[1].world[0] == 2 && scene.instances[1].world[12] == 2 && scene.instances[2].world[12] == 5,
          "Source-space world composition is incorrect");
        instances[0].world[0] = -2;
        instances[0].world[5] = .5f;
        scene.Update(assets, instances);
        Require(
          std::abs(scene.instances[1].normal[0] + .25f) < 1e-6f && scene.instances[1].normal[5] == 1,
          "Mirrored/nonuniform normal matrix is incorrect");
        auto ray = scene.RayModel();
        Require(ray->vertices.size() == 108, "Ray scene lost instance geometry");
        Require(scene.RayModel() == ray, "Unchanged ray scene rebuilt");
        size_t at = 0;
        for (const auto& group : scene.draws)
            for (uint32_t i = 0; i < group.instanceCount; i++)
            {
                const auto& row = scene.instances[group.firstInstance + i];
                for (uint32_t j = 0; j < group.vertexCount; j++, at++)
                {
                    const auto& v = scene.model->vertices[group.firstVertex + j];
                    for (int k = 0; k < 3; k++)
                    {
                        const float expected = row.world[k] * v.position[0] + row.world[4 + k] * v.position[1]
                                               + row.world[8 + k] * v.position[2] + row.world[12 + k];
                        Require(
                          std::abs(ray->vertices[at].position[k] - expected) < 1e-5,
                          "Ray world-space composition mismatch");
                    }
                    Require(ray->vertices[at].position[3] == v.position[3], "Ray material slot changed");
                }
            }
        instances[1].castShadow = false;
        scene.Update(assets, instances);
        Require(scene.RayModel() != ray, "Ray cast edit reused stale model");
        bool noCast = false;
        for (const auto& v : scene.RayModel()->vertices)
            noCast |= v.uv[2] < 0;
        Require(noCast, "Ray caster flag lost");
        auto reject = [&](const std::vector<render::SceneAsset>& a, const std::vector<render::SceneInstance>& i) {
            const auto old      = scene.model;
            bool       rejected = false;
            try
            {
                scene.Update(a, i);
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            Require(rejected, "Invalid scene accepted");
            Require(scene.model == old, "Failed update replaced scene resources");
        };
        auto bad  = instances;
        bad[1].id = bad[0].id;
        reject(assets, bad);
        bad            = instances;
        bad[0].assetId = "missing";
        reject(assets, bad);
        bad             = instances;
        bad[0].world[3] = 1;
        reject(assets, bad);
        bad             = instances;
        bad[0].world[0] = std::numeric_limits<float>::infinity();
        reject(assets, bad);
        auto badAssets  = assets;
        badAssets[1].id = "red";
        reject(badAssets, instances);
        badAssets         = assets;
        badAssets[0].path = root / "missing.glb";
        reject(badAssets, instances);
        badAssets         = assets;
        badAssets[0].path = root / "Fox/Fox.glb";
        reject(badAssets, instances);
        Require(!scene.AssetBounds("missing"), "Unknown bounds should be absent");
        scene.Update({}, {});
        Require(
          scene.active && scene.instances.empty() && scene.model->vertices.empty() && !scene.AssetBounds("red"),
          "Empty scene did not clear");
        std::cout << "PASS scene data: shared resources, source units, transforms, validation, empty scene\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
