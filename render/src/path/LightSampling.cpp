#include "PathTracing.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
namespace render::detail
{
    namespace
    {
        double Luma(double r, double g, double b)
        {
            return .2126 * r + .7152 * g + .0722 * b;
        }
        double Half(uint16_t h)
        {
            const int e = (h >> 10) & 31;
            return (h & 32768 ? -1 : 1)
                   * std::ldexp(e ? 1.0 + (h & 1023) / 1024.0 : (h & 1023) / 1024.0, e ? e - 15 : -14);
        }
        double Corner(double u, double v)
        {
            return std::atan2(u * v, std::sqrt(u * u + v * v + 1));
        }
        double SolidAngle(uint32_t x, uint32_t y, uint32_t size)
        {
            const double u = 2.0 * x / size - 1, v = 2.0 * y / size - 1, d = 2.0 / size;
            return Corner(u + d, v + d) - Corner(u, v + d) - Corner(u + d, v) + Corner(u, v);
        }
        // Quantized CDF differences are the actual sampling probabilities used on the GPU.
        std::vector<float> CDF(const std::vector<double>& weights)
        {
            const double       total = std::accumulate(weights.begin(), weights.end(), 0.0);
            std::vector<float> result(weights.size());
            double             sum = 0;
            for (size_t i = 0; i < weights.size(); ++i)
                result[i] = float((sum += weights[i]) / total);
            if (!result.empty())
                result.back() = 1;
            return result;
        }
    } // namespace
    void PrepareLightSampling(PathScene& scene, bool hdr)
    {
        const auto& model = scene.model;
        const Image white{1, 1, {255, 255, 255, 255}};
        struct EmitterImage
        {
            const Image*         image;
            std::array<float, 3> factor;
            double               mean;
        };
        std::vector<EmitterImage> emitters;
        for (size_t slot = 0; slot < std::max(size_t(1), model.materials.size()); slot++)
        {
            const auto* material = model.materials.empty() ? nullptr : &model.materials[slot];
            const auto& image =
              material ? (material->emissiveTexture ? *material->emissiveTexture : white) : model.images[4];
            const auto factor = material
                                  ? material->emissive
                                  : std::array<float, 3>{model.emissive[0], model.emissive[1], model.emissive[2]};
            double     mean   = 0;
            for (size_t i = 0; i < image.pixels.size(); i += 4)
                mean += Luma(
                  std::pow(image.pixels[i] / 255.0, 2.2) * factor[0],
                  std::pow(image.pixels[i + 1] / 255.0, 2.2) * factor[1],
                  std::pow(image.pixels[i + 2] / 255.0, 2.2) * factor[2]);
            emitters.push_back({&image, factor, mean / std::max(size_t(1), image.pixels.size() / 4)});
        }
        auto emission = [&](const ModelVertex& vertex, double u, double v) {
            const auto& source = emitters.at(model.materials.empty() ? 0 : size_t(vertex.position[3]) - 1);
            if (source.mean == 0)
                return 0.0;
            const auto&  image  = *source.image;
            const auto&  factor = source.factor;
            const auto   mean   = source.mean;
            const double x = (u - std::floor(u)) * image.width - .5, y = (v - std::floor(v)) * image.height - .5;
            const int    bx = int(std::floor(x)), by = int(std::floor(y));
            double       rgb[3]{};
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i)
                {
                    const auto ix  = ((bx + i) % int(image.width) + int(image.width)) % int(image.width),
                               iy  = ((by + j) % int(image.height) + int(image.height)) % int(image.height);
                    const double w = (i ? x - bx : 1 - x + bx) * (j ? y - by : 1 - y + by);
                    for (int k = 0; k < 3; ++k)
                        rgb[k] += w * image.pixels[(iy * image.width + ix) * 4 + k] / 255.0;
                }
            return Luma(
                     std::pow(rgb[0], 2.2) * factor[0],
                     std::pow(rgb[1], 2.2) * factor[1],
                     std::pow(rgb[2], 2.2) * factor[2])
                   + mean * 1e-4;
        };
        std::vector<uint32_t> ids;
        std::vector<double>   areas, weights;
        for (size_t i = 0; i < model.vertices.size(); i += 3)
        {
            const auto& a = model.vertices[i];
            const auto& b = model.vertices[i + 1];
            const auto& c = model.vertices[i + 2];
            double      e[3], f[3], cross[3];
            for (int k = 0; k < 3; ++k)
            {
                e[k] = double(b.position[k]) - a.position[k];
                f[k] = double(c.position[k]) - a.position[k];
            }
            for (int k = 0; k < 3; ++k)
                cross[k] = e[(k + 1) % 3] * f[(k + 2) % 3] - e[(k + 2) % 3] * f[(k + 1) % 3];
            const double area = .5 * std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
            if (area <= 1e-16)
                continue;
            // A positive floor keeps textured emitters with missed bright subregions sampleable.
            const double value =
              (emission(a, a.uv[0], a.uv[1]) + emission(a, b.uv[0], b.uv[1]) + emission(a, c.uv[0], c.uv[1])
               + emission(a, (double(a.uv[0]) + b.uv[0] + c.uv[0]) / 3, (double(a.uv[1]) + b.uv[1] + c.uv[1]) / 3))
              / 4;
            if (value <= 0)
                continue;
            ids.push_back(uint32_t(i / 3));
            areas.push_back(area);
            weights.push_back(area * value);
        }
        const double        areaPower = 2 * 3.141592653589793 * std::accumulate(weights.begin(), weights.end(), 0.0);
        const auto          areaCDF   = CDF(weights);
        const uint32_t      grid      = hdr ? std::min(64u, scene.sky.size) : 64u;
        std::vector<double> envWeights(6 * grid * grid), solid(envWeights.size());
        const uint32_t      size     = hdr ? scene.sky.size : grid;
        double              envPower = 0;
        for (uint32_t face = 0; face < 6; ++face)
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x)
                {
                    const double omega = SolidAngle(x, y, size);
                    double       luma;
                    if (hdr)
                    {
                        const size_t i = ((size_t(face) * size + y) * size + x) * 4;
                        luma =
                          Luma(Half(scene.sky.pixels[i]), Half(scene.sky.pixels[i + 1]), Half(scene.sky.pixels[i + 2]));
                    }
                    else
                    {
                        const double u = 2.0 * (x + .5) / size - 1, v = 2.0 * (y + .5) / size - 1;
                        const double dy = (face == 2   ? 1
                                           : face == 3 ? -1
                                                       : -v)
                                          / std::sqrt(u * u + v * v + 1),
                                     t = dy * .5 + .5;
                        luma           = Luma(.025 + (.45 - .025) * t, .03 + (.52 - .03) * t, .04 + (.65 - .04) * t);
                    }
                    const auto cell = (face * grid + y * grid / size) * grid + x * grid / size;
                    envWeights[cell] += std::max(0.0, luma) * omega;
                    solid[cell] += omega;
                    envPower += std::max(0.0, luma) * omega;
                }
        for (size_t i = 0; i < envWeights.size(); ++i)
            envWeights[i] =
              envPower > 0 ? .95 * envWeights[i] / envPower + .05 * solid[i] / (4 * 3.141592653589793) : solid[i];
        const auto     envCDF = CDF(envWeights);
        const uint32_t header = uint32_t(scene.texels.size() / 4), records = header + 8,
                       pmfs = records + uint32_t(ids.size()) * 4, env = pmfs + uint32_t(model.vertices.size() / 3);
        scene.texels.resize((env + envCDF.size()) * 4);
        auto word = [&](uint32_t at, uint32_t value) { std::memcpy(scene.texels.data() + at * 4, &value, 4); };
        auto real = [&](uint32_t at, float value) { std::memcpy(scene.texels.data() + at * 4, &value, 4); };
        word(header, uint32_t(ids.size()));
        word(header + 1, records);
        word(header + 2, pmfs);
        word(header + 3, env);
        word(header + 4, envPower > 0 ? grid : 0);
        real(header + 5, float(areaPower));
        real(header + 6, float(envPower));
        for (size_t i = 0; i < ids.size(); ++i)
        {
            const float pmf = areaCDF[i] - (i ? areaCDF[i - 1] : 0);
            real(records + uint32_t(i) * 4, areaCDF[i]);
            real(records + uint32_t(i) * 4 + 1, pmf);
            word(records + uint32_t(i) * 4 + 2, ids[i]);
            real(records + uint32_t(i) * 4 + 3, float(areas[i]));
            real(pmfs + ids[i], pmf);
        }
        for (size_t i = 0; i < envCDF.size(); ++i)
            real(env + uint32_t(i), envCDF[i]);
        scene.images[0][3] = header;
    }
} // namespace render::detail
