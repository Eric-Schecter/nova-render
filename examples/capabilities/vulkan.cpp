#include <volk.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    void Check(VkResult result)
    {
        if (result != VK_SUCCESS)
            throw std::runtime_error("Vulkan capability query failed: " + std::to_string(result));
    }

    struct Instance
    {
        VkInstance handle = VK_NULL_HANDLE;
        ~Instance() { if (handle) vkDestroyInstance(handle, nullptr); }
    };
}

void PrintVulkanDevices()
{
    Check(volkInitialize());
    uint32_t loader = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion)
        Check(vkEnumerateInstanceVersion(&loader));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = std::min(loader, uint32_t(VK_API_VERSION_1_2));

    uint32_t count = 0;
    Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
    std::vector<VkExtensionProperties> instanceExtensions(count);
    Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, instanceExtensions.data()));
    const char* portability = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    if (std::any_of(instanceExtensions.begin(), instanceExtensions.end(), [portability](const auto& entry) {
            return std::strcmp(entry.extensionName, portability) == 0;
        }))
    {
        create.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        create.enabledExtensionCount = 1;
        create.ppEnabledExtensionNames = &portability;
    }

    Instance instance;
    Check(vkCreateInstance(&create, nullptr, &instance.handle));
    // Instance-only dispatch leaves Nova's active device dispatch untouched.
    volkLoadInstanceOnly(instance.handle);
    Check(vkEnumeratePhysicalDevices(instance.handle, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count);
    Check(vkEnumeratePhysicalDevices(instance.handle, &count, devices.data()));
    std::cout << "\nVulkan physical devices (reported support, before device feature selection)\n";
    auto print = [](const char* key, auto value) { std::cout << key << " = " << value << '\n'; };
    for (auto device : devices)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        const auto& limits = props.limits;
        print("\nDevice", props.deviceName);
        std::cout << "API = " << VK_API_VERSION_MAJOR(props.apiVersion) << '.' << VK_API_VERSION_MINOR(props.apiVersion)
                  << '.' << VK_API_VERSION_PATCH(props.apiVersion) << '\n';
        print("Vendor ID", props.vendorID);
        print("Device ID", props.deviceID);
        print("Device type (VkPhysicalDeviceType)", props.deviceType);
        print("Driver version (raw)", props.driverVersion);
#define LIMIT(key) print(#key, limits.key)
        LIMIT(maxImageDimension2D); LIMIT(maxImageDimension3D); LIMIT(maxImageArrayLayers);
        LIMIT(maxUniformBufferRange); LIMIT(maxStorageBufferRange); LIMIT(maxBoundDescriptorSets);
        LIMIT(maxPerStageDescriptorSamplers); LIMIT(maxPerStageDescriptorSampledImages);
        LIMIT(maxDescriptorSetSampledImages); LIMIT(maxPerStageDescriptorStorageBuffers);
        LIMIT(maxPerStageDescriptorStorageImages); LIMIT(maxPerStageResources); LIMIT(maxPushConstantsSize);
        LIMIT(maxColorAttachments); LIMIT(maxDrawIndirectCount); LIMIT(maxComputeWorkGroupInvocations);
        LIMIT(maxComputeSharedMemorySize); LIMIT(maxSamplerAnisotropy); LIMIT(timestampPeriod);
        LIMIT(framebufferColorSampleCounts); LIMIT(framebufferDepthSampleCounts);
#undef LIMIT
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(device, &features);
#define FEATURE(key) print(#key, bool(features.key))
        FEATURE(samplerAnisotropy); FEATURE(multiDrawIndirect); FEATURE(drawIndirectFirstInstance);
        FEATURE(textureCompressionBC); FEATURE(textureCompressionETC2); FEATURE(textureCompressionASTC_LDR);
#undef FEATURE
        if (app.apiVersion >= VK_API_VERSION_1_2 && props.apiVersion >= VK_API_VERSION_1_2)
        {
            VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
            VkPhysicalDeviceProperties2 reported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            reported.pNext = &driver;
            vkGetPhysicalDeviceProperties2(device, &reported);
            print("Driver", driver.driverName);
            print("Driver info", driver.driverInfo);
            VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            query.pNext = &v12;
            vkGetPhysicalDeviceFeatures2(device, &query);
#define FEATURE(key) print(#key, bool(v12.key))
            FEATURE(shaderSampledImageArrayNonUniformIndexing); FEATURE(runtimeDescriptorArray);
            FEATURE(descriptorBindingPartiallyBound); FEATURE(descriptorBindingSampledImageUpdateAfterBind);
            FEATURE(drawIndirectCount); FEATURE(bufferDeviceAddress); FEATURE(timelineSemaphore);
#undef FEATURE
        }
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(device, &memory);
        for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
        {
            const auto heap = memory.memoryHeaps[i];
            std::cout << "Memory heap " << i << " = " << heap.size << " bytes, device local = "
                      << bool(heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) << '\n';
        }
        Check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr));
        std::vector<VkExtensionProperties> extensions(count);
        Check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()));
        auto hasExtension = [&](const char* name) {
            return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        };
        for (auto name : {"VK_KHR_ray_query", "VK_KHR_acceleration_structure", "VK_KHR_ray_tracing_pipeline",
                          "VK_EXT_mesh_shader", "VK_EXT_memory_budget"})
            print(name, hasExtension(name));
        if (app.apiVersion >= VK_API_VERSION_1_1 && hasExtension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME))
        {
            VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
            VkPhysicalDeviceMemoryProperties2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
            query.pNext = &budget;
            vkGetPhysicalDeviceMemoryProperties2(device, &query);
            for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
                std::cout << "Memory heap " << i << " budget = " << budget.heapBudget[i]
                          << " bytes, usage = " << budget.heapUsage[i] << " bytes\n";
        }
    }
}
