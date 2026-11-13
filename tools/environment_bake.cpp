// Optional offline asset tool. Uses the stb/GLM dependencies already present in Nova.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <stb_image.h>
#include <stdexcept>
#include <string>
#include <vector>
namespace
{
    constexpr float pi = 3.14159265358979323846f;
    using Vec          = glm::vec3;
    std::array<float, 9> basis(Vec d)
    {
        return {
          0.282095f,
          0.488603f * d.y,
          0.488603f * d.z,
          0.488603f * d.x,
          1.092548f * d.x * d.y,
          1.092548f * d.y * d.z,
          0.315392f * (3 * d.z * d.z - 1),
          1.092548f * d.x * d.z,
          0.546274f * (d.x * d.x - d.y * d.y)};
    }
    Vec faceDirection(int face, float u, float v)
    {
        const Vec rays[] = {{1, -v, -u}, {-1, -v, u}, {u, 1, v}, {u, -1, -v}, {u, -v, 1}, {-u, -v, -1}};
        return glm::normalize(rays[face]);
    }
    uint16_t half(float value)
    {
        value = std::clamp(value, 0.0f, 65504.0f);
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        int      exponent = int((bits >> 23) & 255) - 127 + 15;
        uint32_t mantissa = bits & 0x7fffff;
        if (exponent <= 0)
        {
            if (exponent < -10)
                return 0;
            mantissa = (mantissa | 0x800000) >> (1 - exponent);
            return uint16_t((mantissa + 0x1000) >> 13);
        }
        uint32_t encoded = (uint32_t(exponent) << 10) + ((mantissa + 0x1000) >> 13);
        return uint16_t(std::min(encoded, 0x7bffu));
    }
    float radicalInverse(uint32_t bits)
    {
        bits = (bits << 16) | (bits >> 16);
        bits = ((bits & 0x55555555) << 1) | ((bits & 0xaaaaaaaa) >> 1);
        bits = ((bits & 0x33333333) << 2) | ((bits & 0xcccccccc) >> 2);
        bits = ((bits & 0x0f0f0f0f) << 4) | ((bits & 0xf0f0f0f0) >> 4);
        bits = ((bits & 0x00ff00ff) << 8) | ((bits & 0xff00ff00) >> 8);
        return float(bits) * 2.3283064365386963e-10f;
    }
    struct HDR
    {
        int                                     width = 0, height = 0;
        std::unique_ptr<float, void (*)(void*)> pixels{nullptr, stbi_image_free};
        Vec                                     pixel(int x, int y) const
        {
            x             = (x % width + width) % width;
            y             = std::clamp(y, 0, height - 1);
            const auto* p = pixels.get() + (size_t(y) * width + x) * 3;
            return {p[0], p[1], p[2]};
        }
        Vec sample(Vec d) const
        {
            const float u = (std::atan2(d.z, d.x) / (2 * pi) + 0.5f) * width - 0.5f;
            const float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / pi * height - 0.5f;
            const int   x = int(std::floor(u)), y = int(std::floor(v));
            const float a = u - x, b = v - y;
            return glm::mix(
              glm::mix(pixel(x, y), pixel(x + 1, y), a), glm::mix(pixel(x, y + 1), pixel(x + 1, y + 1), a), b);
        }
    };
    void write32(std::ofstream& file, uint32_t value)
    {
        for (int b = 0; b < 4; ++b)
            file.put(static_cast<char>(value >> (b * 8)));
    }
    void writeFloat(std::ofstream& file, float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        write32(file, bits);
    }
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc < 3 || argc > 5)
            throw std::runtime_error(
              "Usage: nova_environment_bake input.hdr output.nenv [face-size=256] [samples=512]");
        const int face = argc > 3 ? std::stoi(argv[3]) : 256, samples = argc > 4 ? std::stoi(argv[4]) : 512;
        if (face < 16 || face > 1024 || (face & (face - 1)) || samples < 64 || samples > 4096)
            throw std::runtime_error("Face must be power-of-two 16..1024, samples 64..4096");
        HDR image;
        int channels = 0;
        if (
          !stbi_info(argv[1], &image.width, &image.height, &channels) || image.width > 8192 || image.height > 4096
          || image.width < 2 || image.height < 2)
            throw std::runtime_error("Invalid HDR dimensions");
        image.pixels.reset(stbi_loadf(argv[1], &image.width, &image.height, &channels, 3));
        if (!image.pixels)
            throw std::runtime_error("Cannot decode HDR");
        for (size_t i = 0; i < size_t(image.width) * image.height * 3; ++i)
            if (!std::isfinite(image.pixels.get()[i]) || image.pixels.get()[i] < 0)
                throw std::runtime_error("Invalid HDR radiance");
        int levels = 1;
        for (int n = face; n > 1; n /= 2)
            ++levels;
        std::array<glm::dvec3, 9> coefficients{};
        double                    weightSum = 0;
        for (int y = 0; y < image.height; ++y)
        {
            const double theta = pi * (y + 0.5) / image.height, weight = std::sin(theta);
            weightSum += weight * image.width;
            for (int x = 0; x < image.width; ++x)
            {
                const float phi = 2 * pi * ((x + 0.5f) / image.width - 0.5f);
                const Vec   d(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                const auto  sh    = basis(d);
                const auto  color = glm::dvec3(image.pixel(x, y));
                for (int i = 0; i < 9; ++i)
                    coefficients[i] += color * (double(sh[i]) * weight);
            }
        }
        std::ofstream output(argv[2], std::ios::binary);
        if (!output)
            throw std::runtime_error("Cannot write environment");
        output.write("NOVENV01", 8);
        write32(output, face);
        write32(output, levels);
        for (int i = 0; i < 9; ++i)
        {
            const double convolution = i == 0 ? 1.0 : i < 4 ? 2.0 / 3 : 0.25;
            const auto   c           = coefficients[i] * (4 * pi / weightSum * convolution);
            writeFloat(output, float(c.x));
            writeFloat(output, float(c.y));
            writeFloat(output, float(c.z));
            writeFloat(output, 0);
        }
        for (int level = 0, size = face; level < levels; ++level, size = std::max(1, size / 2))
        {
            const float      roughness = float(level) / (levels - 1), alpha = roughness * roughness;
            std::vector<Vec> kernel;
            if (level)
                for (int i = 0; i < samples; ++i)
                {
                    const float phi = 2 * pi * float(i) / samples, xi = radicalInverse(i);
                    const float c = std::sqrt((1 - xi) / (1 + (alpha * alpha - 1) * xi)),
                                s = std::sqrt(std::max(0.0f, 1 - c * c));
                    kernel.push_back({std::cos(phi) * s, std::sin(phi) * s, c});
                }
            for (int f = 0; f < 6; ++f)
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x)
                    {
                        const Vec n = faceDirection(f, 2 * (x + 0.5f) / size - 1, 2 * (y + 0.5f) / size - 1);
                        Vec       color{};
                        if (!level)
                            color = image.sample(n);
                        else
                        {
                            const Vec up = std::abs(n.y) < 0.999f ? Vec(0, 1, 0) : Vec(1, 0, 0),
                                      t = glm::normalize(glm::cross(up, n)), b = glm::cross(n, t);
                            float weight = 0;
                            for (const auto& k : kernel)
                            {
                                const Vec   h = t * k.x + b * k.y + n * k.z, l = 2 * glm::dot(n, h) * h - n;
                                const float nl = std::max(0.0f, glm::dot(n, l));
                                if (nl > 0)
                                {
                                    color += image.sample(l) * nl;
                                    weight += nl;
                                }
                            }
                            color /= std::max(weight, 1e-6f);
                        }
                        for (float value : {color.x, color.y, color.z, 1.0f})
                        {
                            const auto bits = half(value);
                            output.put(static_cast<char>(bits));
                            output.put(static_cast<char>(bits >> 8));
                        }
                    }
            std::cout << "Prefiltered mip " << level << ": " << size << " x " << size << " x 6\n";
        }
        if (!output)
            throw std::runtime_error("Writing environment failed");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
