#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace render::detail {
std::vector<unsigned char> EmbeddedShader(const std::string& name);
std::vector<unsigned char> LoadShader(const std::filesystem::path& directory, const char* name);
}
