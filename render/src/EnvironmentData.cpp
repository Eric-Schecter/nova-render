#include "EnvironmentData.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
namespace render::detail
{
    void ValidateEnvironment(const EnvironmentSettings& settings)
    {
        if (!std::isfinite(settings.rotation) || settings.rotation < -180 || settings.rotation > 180)
            throw std::invalid_argument("Environment rotation must be in [-180,180] degrees");
        for (float value : {settings.intensity, settings.backgroundIntensity})
            if (!std::isfinite(value) || value < 0 || value > 16)
                throw std::invalid_argument("Environment intensities must be in [0,16]");
    }
    const EnvironmentData& FallbackEnvironment()
    {
        static const auto data = [] {
            EnvironmentData value;
            value.mips.push_back({1, std::vector<uint16_t>(24, 0)});
            return value;
        }();
        return data;
    }
    std::shared_ptr<const EnvironmentData> LoadEnvironmentFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 160 || file.tellg() > 128 * 1024 * 1024)
            throw std::runtime_error("Invalid environment file size: " + path.string());
        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0);
        auto u32 = [&] {
            unsigned char bytes[4];
            if (!file.read(reinterpret_cast<char*>(bytes), 4))
                throw std::runtime_error("Truncated environment");
            return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16)
                   | (uint32_t(bytes[3]) << 24);
        };
        char magic[8];
        file.read(magic, 8);
        const bool separateBackground = std::memcmp(magic, "NOVENV02", 8) == 0;
        if (!separateBackground && std::memcmp(magic, "NOVENV01", 8))
            throw std::runtime_error("Expected Nova .nenv environment (NOVENV01/02)");
        const auto face = u32(), levels = u32();
        auto       payloadSize = [](uint32_t faceSize, uint32_t levelCount) {
            if (!faceSize || faceSize > 1024 || (faceSize & (faceSize - 1)))
                throw std::runtime_error("Invalid environment face size");
            uint32_t expectedLevels = 1;
            for (uint32_t n = faceSize; n > 1; n /= 2)
                ++expectedLevels;
            if (levelCount != expectedLevels)
                throw std::runtime_error("Environment must contain a complete mip chain");
            size_t bytes = 0;
            for (uint32_t n = faceSize;; n /= 2)
            {
                bytes += size_t(n) * n * 6 * 8;
                if (n == 1)
                    break;
            }
            return bytes;
        };
        size_t expected = (separateBackground ? 168 : 160) + payloadSize(face, levels);
        auto   result   = std::make_shared<EnvironmentData>();
        for (auto& coefficient : result->irradiance)
            for (float& value : coefficient)
            {
                const auto bits = u32();
                std::memcpy(&value, &bits, 4);
                if (!std::isfinite(value))
                    throw std::runtime_error("Non-finite environment irradiance");
            }
        uint32_t skyFace = 0, skyLevels = 0;
        if (separateBackground)
        {
            skyFace   = u32();
            skyLevels = u32();
            expected += payloadSize(skyFace, skyLevels);
        }
        if (size != expected)
            throw std::runtime_error("Environment payload size mismatch");
        auto readMips = [&](uint32_t firstSize, uint32_t count, std::vector<EnvironmentMip>& mips) {
            for (uint32_t level = 0, n = firstSize; level < count; ++level, n = std::max(1u, n / 2))
            {
                EnvironmentMip mip;
                mip.size = n;
                mip.pixels.resize(size_t(n) * n * 6 * 4);
                for (auto& value : mip.pixels)
                {
                    unsigned char bytes[2]{};
                    file.read(reinterpret_cast<char*>(bytes), 2);
                    value = uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
                    if ((value & 0x7c00) == 0x7c00)
                        throw std::runtime_error("Non-finite environment texel");
                }
                mips.push_back(std::move(mip));
            }
        };
        readMips(face, levels, result->mips);
        if (separateBackground)
            readMips(skyFace, skyLevels, result->backgroundMips);
        if (!file)
            throw std::runtime_error("Cannot read environment: " + path.string());
        return result;
    }
} // namespace render::detail
