#include "ModelData.hpp"
#include <iostream>
#include <stdexcept>
int main()
{
    try
    {
        render::detail::ModelData data;
        data.materials.resize(7);
        data.materials[0].baseColorTexture =
          render::Image{2048, 2048, std::vector<unsigned char>(2048 * 2048 * 4, 255)};
        render::detail::PackMaterials(data);
        size_t bytes = 0;
        for (const auto& image : data.images)
        {
            bytes += image.pixels.size();
            if (image.width > 8192 || image.height > 8192)
                throw std::runtime_error("Atlas dimensions exceed budget");
        }
        if (bytes > 512ull * 1024 * 1024 || data.materialRecords.size() != 7)
            throw std::runtime_error("Large model plus helper materials did not fit");
        std::cout << "PASS adaptive material atlas: seven slots, " << bytes << " bytes\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
