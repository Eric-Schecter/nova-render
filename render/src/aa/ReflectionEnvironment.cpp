#include "ScreenSpaceReflections.hpp"
namespace render::detail
{
    namespace
    {
        uint16_t Half(float v)
        {
            uint32_t x;
            std::memcpy(&x, &v, 4);
            int      e = int((x >> 23) & 255) - 127 + 15;
            uint32_t m = x & 0x7fffff;
            if (e <= 0)
                return 0;
            if (e >= 31)
                return 0x7bff;
            uint32_t round = m + 0x1000;
            if (round & 0x800000)
            {
                round = 0;
                e++;
            }
            return uint16_t((x >> 16 & 0x8000) | (uint32_t(e) << 10) | (round >> 13));
        }
    } // namespace
    AATextureData ReflectionEnvironmentAtlas(const EnvironmentData* env)
    {
        AATextureData out{256, 256, std::vector<unsigned char>(256 * 256 * 8), true};
        for (int level = 0; level < 9; level++)
        {
            int    w = std::max(1, 256 >> level), h = std::max(1, 128 >> level), offset = 256 - (256 >> level);
            double rough = level / 8.0;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    double   phi = ((x + .5) / w - .5) * 6.283185307179586, theta = (y + .5) / h * 3.141592653589793;
                    double   d[3] = {std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
                    uint16_t color[4] = {0, 0, 0, 0x3c00};
                    if (env && !env->mips.empty())
                    {
                        const auto& mip = env->mips[size_t(std::floor(rough * (env->mips.size() - 1) + .5))];
                        double      ax = std::abs(d[0]), ay = std::abs(d[1]), az = std::abs(d[2]), u, v;
                        unsigned    face;
                        if (ax >= ay && ax >= az)
                        {
                            face = d[0] >= 0 ? 0 : 1;
                            u    = (d[0] >= 0 ? -d[2] : d[2]) / ax;
                            v    = -d[1] / ax;
                        }
                        else if (ay >= az)
                        {
                            face = d[1] >= 0 ? 2 : 3;
                            u    = d[0] / ay;
                            v    = (d[1] >= 0 ? d[2] : -d[2]) / ay;
                        }
                        else
                        {
                            face = d[2] >= 0 ? 4 : 5;
                            u    = (d[2] >= 0 ? d[0] : -d[0]) / az;
                            v    = -d[1] / az;
                        }
                        unsigned ix = std::min(mip.size - 1, unsigned((u * .5 + .5) * mip.size)),
                                 iy = std::min(mip.size - 1, unsigned((v * .5 + .5) * mip.size));
                        for (int k = 0; k < 3; k++)
                            color[k] = mip.pixels[((size_t(face) * mip.size + iy) * mip.size + ix) * 4 + k];
                    }
                    else
                    {
                        double up    = std::max(0.0, d[1]),
                               light = std::pow(
                                         std::max(0.0, d[0] * .3 + d[1] * .8 + d[2] * .5196152423),
                                         std::pow(2.0, 8 * (1 - rough)))
                                       * (1 - rough * .75);
                        color[0] = Half(float(.08 + .32 * up + light * 5));
                        color[1] = Half(float(.1 + .4 * up + light * 4));
                        color[2] = Half(float(.14 + .6 * up + light * 3));
                    }
                    std::memcpy(out.pixels.data() + (size_t(offset + y) * 256 + x) * 8, color, 8);
                }
        }
        return out;
    }
} // namespace render::detail
