#include "AnimationData.hpp"
#include "SkinReference.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            return 1;
        const auto model = render::detail::LoadGLB(argv[1]);
        if (
          !model->animation || model->animation->skins.at(0).joints.size() != 11 || model->animation->clips.size() != 2)
            throw std::runtime_error("Missing skin or clips");
        if (!std::any_of(model->animation->vertices.begin(), model->animation->vertices.end(), [](const auto& v) {
                return v.weights.y > 0;
            }))
            throw std::runtime_error("No blended skin vertices");
        render::detail::AnimationPlayer player;
        player.Load(model);
        const auto immutable = player.Model()->vertices;
        const auto first     = ReferenceSkin(*player.Model());
        player.Seek(.5f);
        const auto moved = ReferenceSkin(*player.Model());
        if (
          std::memcmp(immutable.data(), player.Model()->vertices.data(), immutable.size() * sizeof(immutable[0])) != 0)
            throw std::runtime_error("CPU modified bind vertices");
        if (
          player.Model()->jointMatrices.size() * sizeof(render::detail::JointMatrix)
          >= immutable.size() * sizeof(immutable[0]))
            throw std::runtime_error("Palette did not reduce per-frame upload");
        std::ofstream palette(std::string(argv[2]) + ".palette.bin", std::ios::binary);
        palette.write(
          reinterpret_cast<const char*>(player.Model()->jointMatrices.data()),
          player.Model()->jointMatrices.size() * sizeof(render::detail::JointMatrix));
        if (std::memcmp(first.data(), moved.data(), first.size() * sizeof(first[0])) == 0)
            throw std::runtime_error("Bones did not deform mesh");
        std::ofstream output(argv[2], std::ios::binary);
        output.write(reinterpret_cast<const char*>(moved.data()), moved.size() * sizeof(moved[0]));
        for (const auto& v : moved)
        {
            float length = 0;
            for (int k = 0; k < 3; ++k)
            {
                if (!std::isfinite(v.position[k]) || !std::isfinite(v.normal[k]))
                    throw std::runtime_error("Nonfinite pose");
                length += v.normal[k] * v.normal[k];
            }
            if (std::abs(length - 1) > 1e-4f)
                throw std::runtime_error("Invalid normal");
        }
        player.state.playing = false;
        player.Update(.25);
        if (player.state.time != .5f)
            throw std::runtime_error("Pause failed");
        player.state.playing = true;
        player.state.speed   = 2;
        player.Update(.25);
        if (player.state.time != 1)
            throw std::runtime_error("Speed failed");
        player.Select(1);
        player.Seek(.5);
        if (std::memcmp(moved.data(), ReferenceSkin(*player.Model()).data(), moved.size() * sizeof(moved[0])) == 0)
            throw std::runtime_error("Clips identical");
        player.state.loop = false;
        player.Update(3);
        if (player.state.playing || player.state.time != 2)
            throw std::runtime_error("Nonloop failed");
        bool rejected = false;
        try
        {
            player.Seek(-1);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invalid seek accepted");
        std::cout << "PASS 11-joint GPU palette, immutable bind vertices, test reference deformation, normals and "
                     "animation controls\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
