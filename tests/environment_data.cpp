#include "EnvironmentData.hpp"
#include <cmath>
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
