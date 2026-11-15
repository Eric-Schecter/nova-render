#include "TextureFiltering.hpp"
#include <iostream>
#include <stdexcept>
int main()
{
    using namespace render::detail;
    ModelImage image{2, 1, {0, 0, 0, 0, 255, 255, 255, 255}};
    auto       color = MaterialMipmaps(image, 0), data = MaterialMipmaps(image, 2);
    if (color.size() != 2 || color[1].pixels[0] != 188 || color[1].pixels[3] != 128 || data[1].pixels[0] != 128)
        throw std::runtime_error("Color/data mip filtering failed");
    ModelImage odd{3, 1, {0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255}};
    auto       m = MaterialMipmaps(odd, 2);
    if (m[1].pixels[0] != 85)
        throw std::runtime_error("NPOT mip lost edge texels");
    std::cout << "PASS linear-light color, linear data/alpha and NPOT coverage\n";
}
