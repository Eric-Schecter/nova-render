#include "EnvironmentData.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            return 1;
        const auto environment = render::detail::LoadEnvironmentFile(argv[1]);
        if (environment->mips.size() != 9 || environment->mips[0].size != 256)
            throw std::runtime_error("Unexpected prepared environment");
        for (size_t i = 0; i < environment->mips.size(); ++i)
        {
            const auto& mip = environment->mips[i];
            if (mip.pixels.size() != size_t(mip.size) * mip.size * 24 || mip.size != (256u >> i))
                throw std::runtime_error("Invalid environment mip chain");
        }
        const auto& background = environment->backgroundMips;
        if (background.size() != 10 || background.front().size != 512)
            throw std::runtime_error("Expected independent high-resolution background");
        size_t iblBytes = 0;
        for (const auto& mip : environment->mips)
            iblBytes += mip.pixels.size() * 2;
        for (size_t i = 0; i < background.size(); ++i)
            if (
              background[i].size != (512u >> i)
              || background[i].pixels.size() != size_t(background[i].size) * background[i].size * 24)
                throw std::runtime_error("Invalid background mip chain");
        // Version 1 remains readable and falls back to its existing cube for the sky.
        const auto legacyPath = std::filesystem::temp_directory_path() /
            ("nova-environment-legacy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".nenv");
        struct RemoveFile
        {
            std::filesystem::path path;
            ~RemoveFile()
            {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{legacyPath};
        std::ifstream     source(argv[1], std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(source)), {});
        {
            std::ofstream legacy(legacyPath, std::ios::binary);
            legacy.write("NOVENV01", 8);
            legacy.write(bytes.data() + 8, 152);
            legacy.write(bytes.data() + 168, iblBytes);
        }
        const auto old = render::detail::LoadEnvironmentFile(legacyPath);
        if (!old->backgroundMips.empty() || render::detail::BackgroundMips(*old).size() != 9)
            throw std::runtime_error("Legacy environment fallback failed");
        // Reject bad background metadata before attempting a texture allocation.
        bytes[160] = 3;
        bytes[161] = bytes[162] = bytes[163] = 0;
        {
            std::ofstream invalid(legacyPath, std::ios::binary);
            invalid.write(bytes.data(), bytes.size());
        }
        bool invalidRejected = false;
        try
        {
            render::detail::LoadEnvironmentFile(legacyPath);
        }
        catch (const std::runtime_error&)
        {
            invalidRejected = true;
        }
        if (!invalidRejected)
            throw std::runtime_error("Invalid background header accepted");
        if (
          environment->irradiance[0][0] <= 0 || environment->irradiance[0][1] <= 0
          || environment->irradiance[0][2] <= 0)
            throw std::runtime_error("Missing ambient irradiance");
        auto reject = [](render::EnvironmentSettings value) {
            bool rejected = false;
            try
            {
                render::detail::ValidateEnvironment(value);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            if (!rejected)
                throw std::runtime_error("Invalid environment settings accepted");
        };
        render::EnvironmentSettings value;
        render::detail::ValidateEnvironment(value);
        value.intensity           = 0;
        value.backgroundIntensity = 0;
        value.showBackground      = false;
        render::detail::ValidateEnvironment(value);
        value.rotation = 181;
        reject(value);
        value           = {};
        value.intensity = -1;
        reject(value);
        value                     = {};
        value.backgroundIntensity = 17;
        reject(value);
        value          = {};
        value.rotation = std::numeric_limits<float>::quiet_NaN();
        reject(value);
        bool rejected = false;
        try
        {
            render::detail::LoadEnvironmentFile(std::string(argv[1]) + ".missing");
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Missing environment accepted");
        std::cout << "PASS prepared HDR cube, full mip chain, SH irradiance and environment settings\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
