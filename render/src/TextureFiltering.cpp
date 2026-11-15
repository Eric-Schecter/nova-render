#include "TextureFiltering.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace render::detail
{
    std::vector<ModelImage> MaterialMipmaps(const ModelImage& image, size_t slot)
    {
        if (!image.width || !image.height || image.pixels.size() != size_t(image.width) * image.height * 4)
            throw std::invalid_argument("Invalid material image");
        static const auto decode = [] {
            std::array<double, 256> table{};
            for (int i = 0; i < 256; i++)
            {
                double c = i / 255.;
                table[i] = c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
            }
            return table;
        }();
        std::vector<ModelImage> levels{image};
        const bool              color = slot == 0 || slot == 4;
        while (levels.back().width > 1 || levels.back().height > 1)
        {
            const auto& old = levels.back();
            ModelImage  next;
            next.width  = std::max(1u, old.width / 2);
            next.height = std::max(1u, old.height / 2);
            next.pixels.resize(size_t(next.width) * next.height * 4);
            for (uint32_t y = 0; y < next.height; y++)
                for (uint32_t x = 0; x < next.width; x++)
                {
                    const double x0 = double(x) * old.width / next.width, x1 = double(x + 1) * old.width / next.width,
                                 y0 = double(y) * old.height / next.height,
                                 y1 = double(y + 1) * old.height / next.height;
                    double value[4]{}, sum = 0;
                    for (uint32_t sy = uint32_t(y0); sy < std::min(old.height, uint32_t(std::ceil(y1))); sy++)
                        for (uint32_t sx = uint32_t(x0); sx < std::min(old.width, uint32_t(std::ceil(x1))); sx++)
                        {
                            double weight = (std::min(x1, double(sx + 1)) - std::max(x0, double(sx)))
                                            * (std::min(y1, double(sy + 1)) - std::max(y0, double(sy)));
                            sum += weight;
                            for (int c = 0; c < 4; c++)
                            {
                                double v = old.pixels[(size_t(sy) * old.width + sx) * 4 + c] / 255.;
                                if (color && c < 3)
                                    v = decode[old.pixels[(size_t(sy) * old.width + sx) * 4 + c]];
                                value[c] += v * weight;
                            }
                        }
                    for (int c = 0; c < 4; c++)
                    {
                        double v = value[c] / sum;
                        if (color && c < 3)
                            v = v <= .0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - .055;
                        next.pixels[(size_t(y) * next.width + x) * 4 + c] =
                          static_cast<unsigned char>(std::clamp(v, 0., 1.) * 255 + .5);
                    }
                }
            levels.push_back(std::move(next));
        }
        return levels;
    }
} // namespace render::detail
