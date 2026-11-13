#include "ShaderData.hpp"
#include <fstream>
#include <stdexcept>
namespace render::detail
{
    std::vector<unsigned char> LoadShader(const std::filesystem::path& directory, const char* name)
    {
        if (directory.empty())
            return EmbeddedShader(name);
        const auto    path = directory / name;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0)
            throw std::runtime_error("Cannot read shader: " + path.string());
        std::vector<unsigned char> bytes(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Cannot read shader: " + path.string());
        return bytes;
    }
} // namespace render::detail
