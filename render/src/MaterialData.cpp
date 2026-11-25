#include "ModelData.hpp"
#include "Color.hpp"
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
        uint8_t packedChannel(const std::optional<Image>& image, float u, float v, size_t channel)
        {
            if (!image)
                return 255;
            const auto x = uint32_t(std::round(u * (image->width - 1)));
            const auto y = uint32_t(std::round(v * (image->height - 1)));
            return image->pixels[(size_t(y) * image->width + x) * 4 + channel];
        }
    } // namespace
    std::array<const Image*, 8> MaterialTextureSources(const Material& m)
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
    bool CanIndexMaterialTextures(const ModelData& data, size_t maxSlots)
    {
        return !data.materials.empty() && data.materials.size() <= maxSlots
            && std::none_of(data.materials.begin(), data.materials.end(), [](const Material& m) {
                return m.specularTexture || m.specularColorTexture || m.sheenColorTexture || m.sheenRoughnessTexture
                       || m.iridescenceTexture || m.iridescenceThicknessTexture || m.anisotropyTexture
                       || m.alphaMode == AlphaMode::Blend || m.transmissionTexture || m.thicknessTexture;
            });
    }
    bool CanUpdateIndexedMaterialTextures(const ModelData& data, size_t maxSlots)
    {
        return CanIndexMaterialTextures(data, maxSlots) && !HasRasterTransparency(&data)
            && std::all_of(data.materials.begin(), data.materials.end(), [](const Material& m) {
                return m.alphaMode == AlphaMode::Opaque;
            });
    }
    void ValidateMaterial(const Material& m)
    {
        auto range = [](float x, float max) {
            if (!std::isfinite(x) || x < 0 || x > max)
                throw std::invalid_argument("Invalid material factor");
        };
        if (m.baseColorWrap > 10 || (m.baseColorWrap & 3) == 3)
            throw std::invalid_argument("Invalid base-color texture wrapping");
        if (m.emissiveTexCoord > 1)
            throw std::invalid_argument("Emissive texture coordinate set must be 0 or 1");
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
        for (float x : m.sheenColorFactor)
            range(x, 1);
        range(m.sheenRoughnessFactor, 1);
        for (auto uv : m.sheenTexCoords)
            if (uv > 1)
                throw std::invalid_argument("Sheen coordinate set must be 0 or 1");
        range(m.iridescenceFactor, 1);
        if (!std::isfinite(m.iridescenceBaseIor) || m.iridescenceBaseIor < 1 || m.iridescenceBaseIor > 3)
            throw std::invalid_argument("Film substrate index of refraction must be between one and three");
        if (!std::isfinite(m.iridescenceIor) || m.iridescenceIor < 1)
            throw std::invalid_argument("Film index of refraction must be at least one");
        range(m.iridescenceThicknessMinimum, std::numeric_limits<float>::max());
        range(m.iridescenceThicknessMaximum, std::numeric_limits<float>::max());
        for (auto uv : m.iridescenceTexCoords)
            if (uv > 1)
                throw std::invalid_argument("Iridescence coordinate set must be 0 or 1");
        range(m.anisotropyStrength, 1);
        if (!std::isfinite(m.anisotropyRotation) || m.anisotropyTexCoord > 1)
            throw std::invalid_argument("Invalid anisotropy rotation or coordinate set");
        range(m.clearcoatFactor, 1);
        range(m.clearcoatRoughnessFactor, 1);
        if (!std::isfinite(m.clearcoatNormalScale))
            throw std::invalid_argument("Clearcoat normal scale must be finite");
        for (auto uv : m.clearcoatTexCoords)
            if (uv > 1)
                throw std::invalid_argument("Clearcoat coordinate set must be 0 or 1");
        for (auto uv : m.specularTexCoords)
            if (uv > 1) throw std::invalid_argument("Specular coordinate set must be 0 or 1");
        for (float x : m.specularColorFactor)
            range(x, std::numeric_limits<float>::max());
        if (m.independentUVMaps > 3 || (m.independentUVMaps && !m.independentUVTransform))
            throw std::invalid_argument("Invalid independent texture map selection");
        if (m.independentUVTransform)
            for (float x : *m.independentUVTransform)
                if (!std::isfinite(x))
                    throw std::invalid_argument("Invalid independent texture mapping");
        range(m.normalScale, 16);
        range(m.occlusionStrength, 1);
        if (m.occlusionTexCoord > 1)
            throw std::invalid_argument("AO texture coordinate set must be 0 or 1");
        for (float x : m.emissive)
            range(x, 65504);
        range(m.thicknessFactor, std::numeric_limits<float>::max());
        range(m.inverseAttenuationDistance, std::numeric_limits<float>::max());
        for (float x : m.attenuationColor)
            range(x, 1);
        for (const auto* image : {&m.transmissionTexture, &m.thicknessTexture, &m.specularTexture, &m.specularColorTexture, &m.sheenColorTexture, &m.sheenRoughnessTexture, &m.iridescenceTexture, &m.iridescenceThicknessTexture, &m.anisotropyTexture})
            if (*image && (!(*image)->width || !(*image)->height || (*image)->width > 8192 || (*image)->height > 8192
                || (*image)->pixels.size() != size_t((*image)->width) * (*image)->height * 4))
                throw std::invalid_argument("Volume maps require 1..8192 RGBA8 pixels");
        for (const auto* image : MaterialTextureSources(m))
            if (
              !image->width || !image->height || image->width > 8192 || image->height > 8192
              || image->pixels.size() != size_t(image->width) * image->height * 4)
                throw std::invalid_argument("Material maps require 1..8192 RGBA8 pixels");
    }
    void PackMaterials(ModelData& data)
    {
        if (data.materials.empty())
            return;
        if (data.materials.size() > materialSlotLimit)
            throw std::invalid_argument("At most 512 material slots are supported");
        uint32_t tile = 1;
        bool     auxiliaryMaps = false;
        for (const auto& m : data.materials)
        {
            ValidateMaterial(m);
            auxiliaryMaps |= bool(m.clearcoatTexture || m.clearcoatRoughnessTexture || m.clearcoatNormalTexture
              || m.sheenColorTexture || m.sheenRoughnessTexture || m.iridescenceTexture || m.iridescenceThicknessTexture || m.anisotropyTexture);
            for (auto* im : MaterialTextureSources(m))
                tile = std::max({tile, im->width, im->height});
            for (const auto* map : {&m.transmissionTexture, &m.thicknessTexture, &m.specularTexture, &m.specularColorTexture, &m.sheenColorTexture, &m.sheenRoughnessTexture, &m.iridescenceTexture, &m.iridescenceThicknessTexture, &m.anisotropyTexture})
                if (*map)
                    tile = std::max({tile, (*map)->width, (*map)->height});
        }
        auto& first    = data.materials.front();
        data.baseColor = first.baseColor;
        data.material  = {first.metallic, first.roughness, first.normalScale, first.occlusionStrength};
        std::copy(first.emissive.begin(), first.emissive.end(), data.emissive.begin());
        data.materialRecords.clear();
        const bool atlas = data.materials.size() > 1
          || first.transmissionTexture || first.thicknessTexture || first.specularTexture || first.specularColorTexture || first.sheenColorTexture || first.sheenRoughnessTexture || first.iridescenceTexture || first.iridescenceThicknessTexture || first.anisotropyTexture;
        tile             = std::min(tile, 2048u);
        uint32_t cols    = uint32_t(std::ceil(std::sqrt(double(data.materials.size())))),
                 rows = uint32_t((data.materials.size() + cols - 1) / cols), stride = tile + 4, w = cols * stride,
                 h = rows * stride;
        if (atlas)
        {
            // Large models plus small helper materials must fit the same bounded atlas.
            while (tile > 1
                   && (w > 8192 || h > 8192 || uint64_t(w) * h * (auxiliaryMaps ? 32 : 20) > 512ull * 1024 * 1024))
            {
                tile   = std::max(1u, tile / 2);
                stride = tile + 4;
                w      = cols * stride;
                h      = rows * stride;
            }
            if (w > 8192 || h > 8192 || uint64_t(w) * h * (auxiliaryMaps ? 32 : 20) > 512ull * 1024 * 1024)
                throw std::runtime_error("Material atlas exceeds 512 MiB / 8192px limit");
            for (size_t k = 0; k < data.images.size(); ++k)
            {
                auto& image = data.images[k];
                if (k >= 5 && !auxiliaryMaps)
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
            auto images = MaterialTextureSources(first);
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
            record[10] = float((m.specularTexture ? 1u : 0u) | (m.specularColorTexture ? 2u : 0u)
              | (m.specularTexCoords[0] << 2) | (m.specularTexCoords[1] << 3));
            record[11] = m.hybridReflectionStrength.value_or(-1.f);
            std::copy(m.attenuationColor.begin(), m.attenuationColor.end(), record.begin() + 12);
            record[15] = m.thicknessFactor * m.inverseAttenuationDistance;
            std::copy(m.sheenColorFactor.begin(), m.sheenColorFactor.end(), record.begin() + 16);
            record[19] = m.sheenRoughnessFactor;
            record[20] = float((m.sheenColorTexture ? 1u : 0u) | (m.sheenRoughnessTexture ? 2u : 0u)
              | (m.sheenTexCoords[0] << 2) | (m.sheenTexCoords[1] << 3)
              | (m.iridescenceTexture ? 16u : 0u) | (m.iridescenceThicknessTexture ? 32u : 0u)
              | (m.iridescenceTexCoords[0] << 6) | (m.iridescenceTexCoords[1] << 7)
              | (uint32_t(std::lround((m.iridescenceBaseIor - 1) * 2047.5f)) << 8)
              | (m.anisotropyTexture ? 1u << 20 : 0u) | (m.anisotropyTexCoord << 21));
            record[28] = m.iridescenceFactor;
            record[29] = m.iridescenceIor;
            record[30] = m.iridescenceThicknessMinimum;
            record[31] = m.iridescenceThicknessMaximum;
            if (m.independentUVTransform)
            {
                std::copy(m.independentUVTransform->begin(), m.independentUVTransform->end(), record.begin() + 21);
            }
            constexpr double pi = 3.141592653589793;
            const double rotation = std::fmod(double(m.anisotropyRotation), pi) / pi;
            const uint32_t turn = uint32_t(std::lround((rotation < 0 ? rotation + 1 : rotation) * 2048)) & 2047u;
            record[27] = float(m.independentUVMaps | (uint32_t(std::lround(m.anisotropyStrength * 2047)) << 2)
              | (turn << 13));
            std::copy(m.baseColor.begin(), m.baseColor.end(), record.begin() + 32);
            record[36] = m.metallic;
            record[37] = m.roughness;
            record[38] = m.normalScale;
            record[39] = m.occlusionStrength;
            std::copy(m.emissive.begin(), m.emissive.end(), record.begin() + 40);
            record[43] = float(m.alphaMode) + (m.doubleSided ? 4.f : 0.f) + (m.unlit ? 8.f : 0.f) + (m.emissiveTexCoord ? 16.f : 0.f) + float(m.baseColorWrap * 32) + (m.transmissionTexture ? 512.f : 0.f) + (m.thicknessTexture ? 1024.f : 0.f) + m.alphaCutoff * .25f;
            uint32_t x = uint32_t(i) % cols * stride, y = uint32_t(i) / cols * stride;
            if (atlas)
            {
                record[44]  = float(tile - 1) / w;
                record[45]  = float(tile - 1) / h;
                record[46]  = (x + 2.5f) / w;
                record[47]  = (y + 2.5f) / h;
                auto images = MaterialTextureSources(m);
                for (size_t k = 0; k < data.images.size(); k++)
                {
                    if (k >= 5 && !auxiliaryMaps)
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
                            auto* pixel = dst.pixels.data() + (size_t(y + py) * w + x + px) * 4;
                            if (k == 1 && m.specularTexture)
                                pixel[3] = packedChannel(m.specularTexture, u, v, 3);
                            if (k == 3 && m.specularColorTexture)
                                for (size_t channel = 0; channel < 3; ++channel)
                                    pixel[channel + 1] = uint8_t(std::lround(255 * SRGBToLinear(packedChannel(m.specularColorTexture, u, v, channel) / 255.f)));
                            if (k == 5 && m.sheenColorTexture)
                                for (size_t channel = 0; channel < 3; ++channel)
                                    pixel[channel + 1] = uint8_t(std::lround(255 * SRGBToLinear(packedChannel(m.sheenColorTexture, u, v, channel) / 255.f)));
                            if (k == 6)
                            {
                                if (m.sheenRoughnessTexture)
                                    pixel[3] = packedChannel(m.sheenRoughnessTexture, u, v, 3);
                                if (m.iridescenceTexture)
                                    pixel[0] = packedChannel(m.iridescenceTexture, u, v, 0);
                                if (m.iridescenceThicknessTexture)
                                    pixel[2] = packedChannel(m.iridescenceThicknessTexture, u, v, 1);
                            }
                            if (k == 4 && m.anisotropyTexture)
                            {
                                const float dx = packedChannel(m.anisotropyTexture, u, v, 0) * (2.f / 255) - 1;
                                const float dy = packedChannel(m.anisotropyTexture, u, v, 1) * (2.f / 255) - 1;
                                double orientation = std::atan2(dy, dx) / pi;
                                orientation -= std::floor(orientation);
                                pixel[3] = uint8_t(std::lround(orientation * 256));
                            }
                            if (k == 7 && m.anisotropyTexture)
                                pixel[3] = packedChannel(m.anisotropyTexture, u, v, 2);
                            if (k == 2)
                            {
                                pixel[0] = packedChannel(m.thicknessTexture, u, v, 1);
                                pixel[3] = packedChannel(m.transmissionTexture, u, v, 0);
                            }
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
