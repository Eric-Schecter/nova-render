#pragma once
#include "AntiAliasing.hpp"
#include "PostProcessing.hpp"
#include "SceneShadows.hpp"

namespace render
{
    struct ProductShowcaseSettings
    {
        PostProcessingSettings postProcessing;
        SceneShadowSettings    shadows;
        AntiAliasingSettings   antiAliasing;
    };

    constexpr AntiAliasingSettings ProductShowcaseAntiAliasing(bool interacting = false)
    {
        return {interacting ? AAMethod::FXAA : AAMethod::SSAA};
    }

    constexpr ProductShowcaseSettings MakeProductShowcaseSettings(bool interacting = false)
    {
        ProductShowcaseSettings settings{};
        settings.postProcessing.enabled        = true;
        settings.postProcessing.toneMapping    = ToneMapping::ACESFilmic;
        settings.postProcessing.bloomThreshold = 1.82f;
        settings.postProcessing.bloomKnee      = .67f;
        settings.postProcessing.bloomIntensity = .12f;
        settings.shadows                       = {true, 1, 16, .002f};
        settings.antiAliasing                  = ProductShowcaseAntiAliasing(interacting);
        return settings;
    }
} // namespace render
