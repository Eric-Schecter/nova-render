#pragma once
#include "ModelOptions.hpp"
#include <vector>
namespace example
{
    inline ModelOptions ParseProceduralOptions(int argc, char** argv, const char* title)
    {
        // Reuse common window/API/frame flags while supplying no external asset.
        std::vector<char*> args{argv[0], const_cast<char*>("procedural")};
        for (int i = 1; i < argc; i++)
            args.push_back(argv[i]);
        auto options = ParseModelOptions(int(args.size()), args.data(), title);
        if (options.mode != render::RenderMode::Raster)
            throw std::invalid_argument("AA laboratory uses raster rendering");
        return options;
    }
} // namespace example
