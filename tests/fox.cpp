#include "AnimationData.hpp"
#include "SkinReference.hpp"
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
          model->vertices.empty() || model->vertices.front().uv[2] != 1 || !model->animation
          || model->vertices.size() != 1728 || model->animation->skins.at(0).joints.size() != 24
          || model->animation->clips.size() != 3)
            throw std::runtime_error("Fox source metadata mismatch");
        render::detail::AnimationPlayer player;
        player.Load(model);
        player.Seek(.5f);
        const auto vertices = ReferenceSkin(*player.Model());
        for (const auto& v : vertices)
        {
            float length = 0;
            for (int k = 0; k < 3; k++)
            {
                if (!std::isfinite(v.position[k]) || !std::isfinite(v.normal[k]))
                    throw std::runtime_error("Invalid generated Fox normal/pose");
                length += v.normal[k] * v.normal[k];
            }
            if (std::abs(length - 1) > 1e-4f)
                throw std::runtime_error("Fox normal is not unit length");
        }
        std::ofstream out(argv[2], std::ios::binary);
        out.write(reinterpret_cast<const char*>(vertices.data()), vertices.size() * sizeof(vertices[0]));
        for (uint32_t clip = 0; clip < 3; clip++)
        {
            player.Select(clip);
            player.Seek(player.Clips()[clip].duration * .5f);
        }
        std::cout << "PASS original nonindexed Fox, generated normals, 24 joints and Survey/Walk/Run palettes\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
