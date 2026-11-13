#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace probe
{
    constexpr uint32_t ResultBytes = 128;
    inline void        Check(const uint32_t* results)
    {
        auto value = [&](int index) {
            float result;
            std::memcpy(&result, results + index, 4);
            return result;
        };
        auto near = [&](int index, float expected) {
            const auto f = value(index);
            return std::isfinite(f) && std::abs(f - expected) <= 0.0001f;
        };
        if (
          results[0] != 1 || !near(1, 1) || results[2] != 0 || !near(4, 0.25f) || !near(5, 0.5f) || results[6] != 0
          || results[7] != 1 || results[8] != 0 || results[16] != 1 || !near(17, 1) || results[23] != 0
          || results[24] != 0)
            throw std::runtime_error("Ray query readback did not match hit/miss, barycentrics, facing or clipping");
        std::cout
          << "PASS: front/back hits, distance, primitive/instance index, barycentrics, miss and near clipping\n";
    }
} // namespace probe
