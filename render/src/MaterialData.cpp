#include "ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stb_image.h>
#include <stdexcept>
namespace render
{
    Image LoadMaterialImage(const std::filesystem::path& path)
    {
        int   w, h, c;
        auto* bytes = stbi_load(path.string().c_str(), &w, &h, &c, 4);
        if (!bytes)
            throw std::runtime_error("Cannot decode material image: " + path.string());
        if (w < 1 || h < 1 || w > 8192 || h > 8192)
        {
            stbi_image_free(bytes);
            throw std::invalid_argument("Material image dimensions must be 1..8192");
        }
        Image image{uint32_t(w), uint32_t(h), {}};
        try
        {
            image.pixels.assign(bytes, bytes + size_t(w) * h * 4);
        }
        catch (...)
        {
            stbi_image_free(bytes);
            throw;
        }
        stbi_image_free(bytes);
        return image;
    }
} // namespace render
namespace render::detail
{
    namespace
    {
        const std::array<Image, 8> defaults{
          {{1, 1, {255, 255, 255, 255}},
           {1, 1, {128, 128, 255, 255}},
           {1, 1, {255, 255, 255, 255}},
           {1, 1, {255, 255, 255, 255}},
           {1, 1, {255, 255, 255, 255}},
           {1, 1, {255, 255, 255, 255}},
           {1, 1, {255, 255, 255, 255}},
           {1, 1, {128, 128, 255, 255}}}};
        std::array<const Image*, 8> maps(const Material& m)
        {
            return {
              m.baseColorTexture ? &*m.baseColorTexture : &defaults[0],
              m.normalTexture ? &*m.normalTexture : &defaults[1],
              m.metallicRoughnessTexture ? &*m.metallicRoughnessTexture : &defaults[2],
              m.occlusionTexture ? &*m.occlusionTexture : &defaults[3],
              m.emissiveTexture ? &*m.emissiveTexture : &defaults[4],
              m.clearcoatTexture ? &*m.clearcoatTexture : &defaults[5],
              m.clearcoatRoughnessTexture ? &*m.clearcoatRoughnessTexture : &defaults[6],
              m.clearcoatNormalTexture ? &*m.clearcoatNormalTexture : &defaults[7]};
        }
    } // namespace
    void ValidateMaterial(const Material& m)
    {
        auto range = [](float x, float max) {
            if (!std::isfinite(x) || x < 0 || x > max)
                throw std::invalid_argument("Invalid material factor");
        };
        if (unsigned(m.alphaMode) > 2)
            throw std::invalid_argument("Invalid alpha mode");
        range(m.alphaCutoff, 1);
        for (float x : m.baseColor)
            range(x, 1);
        range(m.roughness, 1);
        range(m.metallic, 1);
        range(m.specularFactor, 1);
        if (m.hybridReflectionStrength)
            range(*m.hybridReflectionStrength, 1);
        range(m.clearcoatFactor, 1);
        range(m.clearcoatRoughnessFactor, 1);
        if (!std::isfinite(m.clearcoatNormalScale))
            throw std::invalid_argument("Clearcoat normal scale must be finite");
        for (auto uv : m.clearcoatTexCoords)
            if (uv > 1)
                throw std::invalid_argument("Clearcoat coordinate set must be 0 or 1");
        for (float x : m.specularColorFactor)
            range(x, std::numeric_limits<float>::max());
        range(m.normalScale, 16);
        range(m.occlusionStrength, 1);
        if (m.occlusionTexCoord > 1)
            throw std::invalid_argument("AO texture coordinate set must be 0 or 1");
        for (float x : m.emissive)
            range(x, 65504);
        for (const auto* image : maps(m))
            if (
              !image->width || !image->height || image->width > 8192 || image->height > 8192
              || image->pixels.size() != size_t(image->width) * image->height * 4)
                throw std::invalid_argument("Material maps require 1..8192 RGBA8 pixels");
    }
    void PackMaterials(ModelData& data)
    {
        if (data.materials.empty())
            return;
        if (data.materials.size() > 128)
            throw std::invalid_argument("At most 128 material slots are supported");
        uint32_t tile = 1;
        bool     texturedCoat = false;
        for (const auto& m : data.materials)
        {
            ValidateMaterial(m);
            texturedCoat |= bool(m.clearcoatTexture || m.clearcoatRoughnessTexture || m.clearcoatNormalTexture);
            for (auto* im : maps(m))
                tile = std::max({tile, im->width, im->height});
        }
        auto& first    = data.materials.front();
        data.baseColor = first.baseColor;
        data.material  = {first.metallic, first.roughness, first.normalScale, first.occlusionStrength};
        std::copy(first.emissive.begin(), first.emissive.end(), data.emissive.begin());
        data.materialRecords.clear();
        const bool atlas = data.materials.size() > 1;
        tile             = std::min(tile, 2048u);
        uint32_t cols    = uint32_t(std::ceil(std::sqrt(double(data.materials.size())))),
                 rows = uint32_t((data.materials.size() + cols - 1) / cols), stride = tile + 4, w = cols * stride,
                 h = rows * stride;
        if (atlas)
        {
            // Large models plus small helper materials must fit the same bounded atlas.
            while (tile > 1
                   && (w > 8192 || h > 8192 || uint64_t(w) * h * (texturedCoat ? 32 : 20) > 512ull * 1024 * 1024))
            {
                tile   = std::max(1u, tile / 2);
                stride = tile + 4;
                w      = cols * stride;
                h      = rows * stride;
            }
            if (w > 8192 || h > 8192 || uint64_t(w) * h * (texturedCoat ? 32 : 20) > 512ull * 1024 * 1024)
                throw std::runtime_error("Material atlas exceeds 512 MiB / 8192px limit");
            for (size_t k = 0; k < data.images.size(); ++k)
            {
                auto& image = data.images[k];
                if (k >= 5 && !texturedCoat)
                {
                    image = defaults[k];
                    continue;
                }
                image.width  = w;
                image.height = h;
                image.pixels.assign(size_t(w) * h * 4, 0);
            }
        }
        else
        {
            auto images = maps(first);
            for (size_t k = 0; k < data.images.size(); k++)
                data.images[k] = *images[k];
        }
        for (size_t i = 0; i < data.materials.size(); i++)
        {
            const auto&           m = data.materials[i];
            std::array<float, 48> record{};
            for (int k : {0, 5, 10, 15, 16, 21, 26, 31})
                record[k] = 1;
            // Material records reuse the unused world matrix.
            std::copy(m.specularColorFactor.begin(), m.specularColorFactor.end(), record.begin());
            record[3] = m.specularFactor;
            record[4] = m.clearcoatFactor;
            record[5] = m.clearcoatRoughnessFactor;
            record[6] = float(m.occlusionTexCoord);
            record[7] = float(
              (m.clearcoatTexture ? 1 : 0) | (m.clearcoatRoughnessTexture ? 2 : 0)
              | (m.clearcoatNormalTexture ? 4 : 0));
            record[8] =
              float(m.clearcoatTexCoords[0] | (m.clearcoatTexCoords[1] << 1) | (m.clearcoatTexCoords[2] << 2));
            record[9]  = m.clearcoatNormalScale;
            record[11] = m.hybridReflectionStrength.value_or(-1.f);
            std::copy(m.baseColor.begin(), m.baseColor.end(), record.begin() + 32);
            record[36] = m.metallic;
            record[37] = m.roughness;
            record[38] = m.normalScale;
            record[39] = m.occlusionStrength;
            std::copy(m.emissive.begin(), m.emissive.end(), record.begin() + 40);
            record[43] = float(m.alphaMode) + (m.doubleSided ? 4.f : 0.f) + m.alphaCutoff * .25f;
            uint32_t x = uint32_t(i) % cols * stride, y = uint32_t(i) / cols * stride;
            if (atlas)
            {
                record[44]  = float(tile - 1) / w;
                record[45]  = float(tile - 1) / h;
                record[46]  = (x + 2.5f) / w;
                record[47]  = (y + 2.5f) / h;
                auto images = maps(m);
                for (size_t k = 0; k < data.images.size(); k++)
                {
                    if (k >= 5 && !texturedCoat)
                        break;
                    const auto& src = *images[k];
                    auto&       dst = data.images[k];
                    for (uint32_t py = 0; py < stride; py++)
                        for (uint32_t px = 0; px < stride; px++)
                        {
                            float u     = std::clamp((float(px) - 2) / std::max(1u, tile - 1), 0.f, 1.f),
                                  v     = std::clamp((float(py) - 2) / std::max(1u, tile - 1), 0.f, 1.f);
                            uint32_t sx = uint32_t(std::round(u * (src.width - 1))),
                                     sy = uint32_t(std::round(v * (src.height - 1)));
                            std::memcpy(
                              dst.pixels.data() + (size_t(y + py) * w + x + px) * 4,
                              src.pixels.data() + (size_t(sy) * src.width + sx) * 4,
                              4);
                        }
                }
            }
            else
            {
                record[44] = record[45] = 1;
            }
            data.materialRecords.push_back(record);
        }
    }
} // namespace render::detail
