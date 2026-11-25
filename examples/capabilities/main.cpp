#include "render/Window.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>

#ifdef NOVA_CAPABILITIES_VULKAN
void PrintVulkanDevices();
#endif
#ifdef __APPLE__
void PrintMetalDevices();
#elif defined(_WIN32)
void PrintDX12Devices();
#endif

int main(int argc, char** argv)
{
    try
    {
        render::WindowConfig config;
        config.title = "Nova Render | Capabilities";
        if (argc == 3 && std::string_view(argv[1]) == "--api")
        {
            const std::string_view api = argv[2];
            if (api == "metal")
                config.renderer.api = render::GraphicsAPI::Metal;
            else if (api == "vulkan")
                config.renderer.api = render::GraphicsAPI::Vulkan;
            else if (api == "dx12")
                config.renderer.api = render::GraphicsAPI::D3D12;
            else if (api != "auto")
                throw std::invalid_argument("Unknown graphics API");
        }
        else if (argc != 1)
            throw std::invalid_argument("Usage: nova_capabilities [--api auto|metal|vulkan|dx12]");

        render::GraphicsAPI selected;
        {
            render::Window window(config);
            selected = window.GetAPI();
            std::cout << std::boolalpha << "\nNova Render capabilities\n"
                      << "Backend = " << render::GraphicsAPIName(window.GetAPI()) << '\n';
            auto print = [](const char* key, const auto& value) { std::cout << key << " = " << value << '\n'; };
    #define FIELD(group, key) print(#group "." #key, group.key)
            const auto ray = window.GetCapabilities();
            FIELD(ray, rayTracing); FIELD(ray, pathTracing); FIELD(ray, softwareRayTracing);
            FIELD(ray, metropolis); FIELD(ray, rayTracingDevice); FIELD(ray, reason);
            const auto raster = window.GetRasterCapabilities();
            FIELD(raster, forward); FIELD(raster, deferred); FIELD(raster, gBuffer);
            FIELD(raster, forwardPlus); FIELD(raster, clusteredForward); FIELD(raster, deferredMSAA);
            FIELD(raster, opaqueOnly); FIELD(raster, debugBypassesAAAndPost);
            FIELD(raster, deferredRequiresModel); FIELD(raster, gBufferTargets); FIELD(raster, gBufferBytesPerPixel);
            const auto hybrid = window.GetHybridCapabilities();
            FIELD(hybrid, supported); FIELD(hybrid, rasterPrimaryVisibility); FIELD(hybrid, shadows);
            FIELD(hybrid, softwareRayTracing); FIELD(hybrid, hardwareRayTracing); FIELD(hybrid, ambientOcclusion);
            FIELD(hybrid, reflections); FIELD(hybrid, globalIllumination); FIELD(hybrid, temporalReconstruction);
            FIELD(hybrid, animatedGeometry); FIELD(hybrid, taa); FIELD(hybrid, splitAA); FIELD(hybrid, probeGI);
            FIELD(hybrid, gpuPresentation); FIELD(hybrid, alphaMask); FIELD(hybrid, alphaBlend);
            FIELD(hybrid, execution); FIELD(hybrid, reason); FIELD(hybrid, sparseShadowFallback);
            const auto material = window.GetMaterialCapabilities();
            FIELD(material, opaque); FIELD(material, mask); FIELD(material, blend); FIELD(material, doubleSided);
            FIELD(material, maskShadows); FIELD(material, blendShadows); FIELD(material, maskRayTracing);
            FIELD(material, blendRayTracing); FIELD(material, rayMaterialSidedness); FIELD(material, alphaAwarePicking);
            FIELD(material, blendMSAA); FIELD(material, blendTAA); FIELD(material, blendAASplit); FIELD(material, transparency);
            const auto lighting = window.GetLightingCapabilities();
            FIELD(lighting, directional); FIELD(lighting, point); FIELD(lighting, spot); FIELD(lighting, rectangle);
            FIELD(lighting, maxLights); FIELD(lighting, directionalShadows); FIELD(lighting, pointShadows);
            FIELD(lighting, spotShadows); FIELD(lighting, rectangleShadows); FIELD(lighting, shadowRequiresScene);
            FIELD(lighting, collectionLightSubpaths); FIELD(lighting, collectionPhotonEmission);
            FIELD(lighting, estimator); FIELD(lighting, reason);
            for (auto type : lighting.sourceLightTypes)
                print("lighting.sourceLightType", static_cast<unsigned>(type));
            const auto animation = window.GetAnimationCapabilities();
            FIELD(animation, skeletal); FIELD(animation, nodeTRS); FIELD(animation, morph);
            FIELD(animation, instancePlayback); FIELD(animation, gpuSkinning); FIELD(animation, manualClock);
            FIELD(animation, dynamicShadows); FIELD(animation, posedPicking); FIELD(animation, posedBounds);
            FIELD(animation, rayTracing); FIELD(animation, hybrid); FIELD(animation, minimumSpeed);
            FIELD(animation, maximumSpeed); FIELD(animation, reason);
#undef FIELD
        }
#ifdef NOVA_CAPABILITIES_VULKAN
        if (selected == render::GraphicsAPI::Vulkan)
            PrintVulkanDevices();
#endif
#ifdef __APPLE__
        if (selected == render::GraphicsAPI::Metal)
            PrintMetalDevices();
#elif defined(_WIN32)
        if (selected == render::GraphicsAPI::D3D12)
            PrintDX12Devices();
#endif
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
