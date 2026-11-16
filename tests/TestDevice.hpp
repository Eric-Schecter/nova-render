#pragma once
#include <string>

// Existing "dx12" tests retain WARP coverage; hardware is an explicit opt-in.
inline std::string TestAPI(const char* argument)
{
    return std::string(argument) == "dx12-hardware" ? "dx12" : argument;
}
inline bool TestUseWarp(const char* argument)
{
    return argument && std::string(argument) == "dx12";
}
