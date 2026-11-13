#pragma once
#include "render/Model.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
namespace render::detail
{
    struct EnvironmentMip
    {
        uint32_t size = 1;
        // Six faces, each tightly packed RGBA16F. Face order: +X, -X, +Y, -Y, +Z, -Z.
        std::vector<uint16_t> pixels;
    };
    struct EnvironmentData
    {
        std::array<std::array<float, 4>, 9> irradiance{};
        std::vector<EnvironmentMip>         mips;
    };
    std::shared_ptr<const EnvironmentData> LoadEnvironmentFile(const std::filesystem::path& path);
    const EnvironmentData&                 FallbackEnvironment();
    void                                   ValidateEnvironment(const EnvironmentSettings& settings);
} // namespace render::detail
