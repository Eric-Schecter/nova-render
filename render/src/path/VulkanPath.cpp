#include "PathTracing.hpp"
#include "ShaderData.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#include <volk.h>
namespace render::detail
{
    namespace
    {
        std::atomic<uint32_t>          validationErrors{0};
        VKAPI_ATTR VkBool32 VKAPI_CALL Debug(
          VkDebugUtilsMessageSeverityFlagBitsEXT severity,
          VkDebugUtilsMessageTypeFlagsEXT,
          const VkDebugUtilsMessengerCallbackDataEXT* data,
          void*)
        {
            if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                ++validationErrors;
            std::cerr << data->pMessage << '\n';
            return VK_FALSE;
        }
        void Check(VkResult result)
        {
            if (result != VK_SUCCESS)
                throw std::runtime_error("Vulkan call failed: " + std::to_string(result));
        }
        struct Buffer
        {
            VkBuffer        handle{};
            VkDeviceMemory  memory{};
            VkDeviceAddress address{};
        };
        struct RayApi
        {
            PFN_vkAllocateCommandBuffers                   vkAllocateCommandBuffers{};
            PFN_vkAllocateDescriptorSets                   vkAllocateDescriptorSets{};
            PFN_vkAllocateMemory                           vkAllocateMemory{};
            PFN_vkBeginCommandBuffer                       vkBeginCommandBuffer{};
            PFN_vkBindBufferMemory                         vkBindBufferMemory{};
            PFN_vkCmdBindDescriptorSets                    vkCmdBindDescriptorSets{};
            PFN_vkCmdBindPipeline                          vkCmdBindPipeline{};
            PFN_vkCmdBuildAccelerationStructuresKHR        vkCmdBuildAccelerationStructuresKHR{};
            PFN_vkCmdDispatch                              vkCmdDispatch{};
            PFN_vkCmdPipelineBarrier                       vkCmdPipelineBarrier{};
            PFN_vkCreateAccelerationStructureKHR           vkCreateAccelerationStructureKHR{};
            PFN_vkCreateBuffer                             vkCreateBuffer{};
            PFN_vkCreateCommandPool                        vkCreateCommandPool{};
            PFN_vkCreateComputePipelines                   vkCreateComputePipelines{};
            PFN_vkCreateDebugUtilsMessengerEXT             vkCreateDebugUtilsMessengerEXT{};
            PFN_vkCreateDescriptorPool                     vkCreateDescriptorPool{};
            PFN_vkCreateDescriptorSetLayout                vkCreateDescriptorSetLayout{};
            PFN_vkCreateDevice                             vkCreateDevice{};
            PFN_vkCreatePipelineLayout                     vkCreatePipelineLayout{};
            PFN_vkCreateShaderModule                       vkCreateShaderModule{};
            PFN_vkDestroyAccelerationStructureKHR          vkDestroyAccelerationStructureKHR{};
            PFN_vkDestroyBuffer                            vkDestroyBuffer{};
            PFN_vkDestroyCommandPool                       vkDestroyCommandPool{};
            PFN_vkDestroyDebugUtilsMessengerEXT            vkDestroyDebugUtilsMessengerEXT{};
            PFN_vkDestroyDescriptorPool                    vkDestroyDescriptorPool{};
            PFN_vkDestroyDescriptorSetLayout               vkDestroyDescriptorSetLayout{};
            PFN_vkDestroyDevice                            vkDestroyDevice{};
            PFN_vkDestroyInstance                          vkDestroyInstance{};
            PFN_vkDestroyPipeline                          vkDestroyPipeline{};
            PFN_vkDestroyPipelineLayout                    vkDestroyPipelineLayout{};
            PFN_vkDestroyShaderModule                      vkDestroyShaderModule{};
            PFN_vkDeviceWaitIdle                           vkDeviceWaitIdle{};
            PFN_vkEndCommandBuffer                         vkEndCommandBuffer{};
            PFN_vkEnumerateDeviceExtensionProperties       vkEnumerateDeviceExtensionProperties{};
            PFN_vkEnumeratePhysicalDevices                 vkEnumeratePhysicalDevices{};
            PFN_vkFreeMemory                               vkFreeMemory{};
            PFN_vkGetAccelerationStructureBuildSizesKHR    vkGetAccelerationStructureBuildSizesKHR{};
            PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR{};
            PFN_vkGetBufferDeviceAddress                   vkGetBufferDeviceAddress{};
            PFN_vkGetBufferMemoryRequirements              vkGetBufferMemoryRequirements{};
            PFN_vkGetDeviceQueue                           vkGetDeviceQueue{};
            PFN_vkGetPhysicalDeviceFeatures2               vkGetPhysicalDeviceFeatures2{};
            PFN_vkGetPhysicalDeviceMemoryProperties        vkGetPhysicalDeviceMemoryProperties{};
            PFN_vkGetPhysicalDeviceProperties              vkGetPhysicalDeviceProperties{};
            PFN_vkGetPhysicalDeviceProperties2             vkGetPhysicalDeviceProperties2{};
            PFN_vkGetPhysicalDeviceQueueFamilyProperties   vkGetPhysicalDeviceQueueFamilyProperties{};
            PFN_vkMapMemory                                vkMapMemory{};
            PFN_vkQueueSubmit                              vkQueueSubmit{};
            PFN_vkQueueWaitIdle                            vkQueueWaitIdle{};
            PFN_vkResetCommandBuffer                       vkResetCommandBuffer{};
            PFN_vkUnmapMemory                              vkUnmapMemory{};
            PFN_vkUpdateDescriptorSets                     vkUpdateDescriptorSets{};
            void                                           Load(VkInstance instance)
            {
                vkAllocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(
                  vkGetInstanceProcAddr(instance, "vkAllocateCommandBuffers"));
                vkAllocateDescriptorSets = reinterpret_cast<PFN_vkAllocateDescriptorSets>(
                  vkGetInstanceProcAddr(instance, "vkAllocateDescriptorSets"));
                vkAllocateMemory =
                  reinterpret_cast<PFN_vkAllocateMemory>(vkGetInstanceProcAddr(instance, "vkAllocateMemory"));
                vkBeginCommandBuffer =
                  reinterpret_cast<PFN_vkBeginCommandBuffer>(vkGetInstanceProcAddr(instance, "vkBeginCommandBuffer"));
                vkBindBufferMemory =
                  reinterpret_cast<PFN_vkBindBufferMemory>(vkGetInstanceProcAddr(instance, "vkBindBufferMemory"));
                vkCmdBindDescriptorSets = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(
                  vkGetInstanceProcAddr(instance, "vkCmdBindDescriptorSets"));
                vkCmdBindPipeline =
                  reinterpret_cast<PFN_vkCmdBindPipeline>(vkGetInstanceProcAddr(instance, "vkCmdBindPipeline"));
                vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
                  vkGetInstanceProcAddr(instance, "vkCmdBuildAccelerationStructuresKHR"));
                vkCmdDispatch = reinterpret_cast<PFN_vkCmdDispatch>(vkGetInstanceProcAddr(instance, "vkCmdDispatch"));
                vkCmdPipelineBarrier =
                  reinterpret_cast<PFN_vkCmdPipelineBarrier>(vkGetInstanceProcAddr(instance, "vkCmdPipelineBarrier"));
                vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
                  vkGetInstanceProcAddr(instance, "vkCreateAccelerationStructureKHR"));
                vkCreateBuffer =
                  reinterpret_cast<PFN_vkCreateBuffer>(vkGetInstanceProcAddr(instance, "vkCreateBuffer"));
                vkCreateCommandPool =
                  reinterpret_cast<PFN_vkCreateCommandPool>(vkGetInstanceProcAddr(instance, "vkCreateCommandPool"));
                vkCreateComputePipelines = reinterpret_cast<PFN_vkCreateComputePipelines>(
                  vkGetInstanceProcAddr(instance, "vkCreateComputePipelines"));
                vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                  vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
                vkCreateDescriptorPool = reinterpret_cast<PFN_vkCreateDescriptorPool>(
                  vkGetInstanceProcAddr(instance, "vkCreateDescriptorPool"));
                vkCreateDescriptorSetLayout = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(
                  vkGetInstanceProcAddr(instance, "vkCreateDescriptorSetLayout"));
                vkCreateDevice =
                  reinterpret_cast<PFN_vkCreateDevice>(vkGetInstanceProcAddr(instance, "vkCreateDevice"));
                vkCreatePipelineLayout = reinterpret_cast<PFN_vkCreatePipelineLayout>(
                  vkGetInstanceProcAddr(instance, "vkCreatePipelineLayout"));
                vkCreateShaderModule =
                  reinterpret_cast<PFN_vkCreateShaderModule>(vkGetInstanceProcAddr(instance, "vkCreateShaderModule"));
                vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
                  vkGetInstanceProcAddr(instance, "vkDestroyAccelerationStructureKHR"));
                vkDestroyBuffer =
                  reinterpret_cast<PFN_vkDestroyBuffer>(vkGetInstanceProcAddr(instance, "vkDestroyBuffer"));
                vkDestroyCommandPool =
                  reinterpret_cast<PFN_vkDestroyCommandPool>(vkGetInstanceProcAddr(instance, "vkDestroyCommandPool"));
                vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                  vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
                vkDestroyDescriptorPool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(
                  vkGetInstanceProcAddr(instance, "vkDestroyDescriptorPool"));
                vkDestroyDescriptorSetLayout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(
                  vkGetInstanceProcAddr(instance, "vkDestroyDescriptorSetLayout"));
                vkDestroyDevice =
                  reinterpret_cast<PFN_vkDestroyDevice>(vkGetInstanceProcAddr(instance, "vkDestroyDevice"));
                vkDestroyInstance =
                  reinterpret_cast<PFN_vkDestroyInstance>(vkGetInstanceProcAddr(instance, "vkDestroyInstance"));
                vkDestroyPipeline =
                  reinterpret_cast<PFN_vkDestroyPipeline>(vkGetInstanceProcAddr(instance, "vkDestroyPipeline"));
                vkDestroyPipelineLayout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(
                  vkGetInstanceProcAddr(instance, "vkDestroyPipelineLayout"));
                vkDestroyShaderModule =
                  reinterpret_cast<PFN_vkDestroyShaderModule>(vkGetInstanceProcAddr(instance, "vkDestroyShaderModule"));
                vkDeviceWaitIdle =
                  reinterpret_cast<PFN_vkDeviceWaitIdle>(vkGetInstanceProcAddr(instance, "vkDeviceWaitIdle"));
                vkEndCommandBuffer =
                  reinterpret_cast<PFN_vkEndCommandBuffer>(vkGetInstanceProcAddr(instance, "vkEndCommandBuffer"));
                vkEnumerateDeviceExtensionProperties = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
                  vkGetInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties"));
                vkEnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
                  vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
                vkFreeMemory = reinterpret_cast<PFN_vkFreeMemory>(vkGetInstanceProcAddr(instance, "vkFreeMemory"));
                vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
                  vkGetInstanceProcAddr(instance, "vkGetAccelerationStructureBuildSizesKHR"));
                vkGetAccelerationStructureDeviceAddressKHR =
                  reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
                    vkGetInstanceProcAddr(instance, "vkGetAccelerationStructureDeviceAddressKHR"));
                vkGetBufferDeviceAddress = reinterpret_cast<PFN_vkGetBufferDeviceAddress>(
                  vkGetInstanceProcAddr(instance, "vkGetBufferDeviceAddress"));
                vkGetBufferMemoryRequirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(
                  vkGetInstanceProcAddr(instance, "vkGetBufferMemoryRequirements"));
                vkGetDeviceQueue =
                  reinterpret_cast<PFN_vkGetDeviceQueue>(vkGetInstanceProcAddr(instance, "vkGetDeviceQueue"));
                vkGetPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
                  vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
                vkGetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
                  vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties"));
                vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
                  vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"));
                vkGetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
                  vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2"));
                vkGetPhysicalDeviceQueueFamilyProperties =
                  reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
                    vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
                vkMapMemory   = reinterpret_cast<PFN_vkMapMemory>(vkGetInstanceProcAddr(instance, "vkMapMemory"));
                vkQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(vkGetInstanceProcAddr(instance, "vkQueueSubmit"));
                vkQueueWaitIdle =
                  reinterpret_cast<PFN_vkQueueWaitIdle>(vkGetInstanceProcAddr(instance, "vkQueueWaitIdle"));
                vkResetCommandBuffer =
                  reinterpret_cast<PFN_vkResetCommandBuffer>(vkGetInstanceProcAddr(instance, "vkResetCommandBuffer"));
                vkUnmapMemory = reinterpret_cast<PFN_vkUnmapMemory>(vkGetInstanceProcAddr(instance, "vkUnmapMemory"));
                vkUpdateDescriptorSets = reinterpret_cast<PFN_vkUpdateDescriptorSets>(
                  vkGetInstanceProcAddr(instance, "vkUpdateDescriptorSets"));
            }
        };
        struct Context
        {
            RayApi                                  api;
            VkDebugUtilsMessengerEXT                messenger{};
            VkInstance                              instance{};
            VkDevice                                device{};
            VkPhysicalDevice                        physical{};
            VkQueue                                 queue{};
            VkCommandPool                           commands{};
            VkDescriptorPool                        descriptors{};
            VkDescriptorSetLayout                   layout{};
            VkPipelineLayout                        pipelineLayout{};
            VkPipeline                              pipeline{}, denoisePipeline{};
            VkShaderModule                          shader{}, denoiseShader{};
            std::array<VkShaderModule, 2>           photonShaders{};
            std::array<VkPipeline, 2>               photonPipelines{};
            std::array<VkShaderModule, 3>           mltShaders{};
            std::array<VkPipeline, 3>               mltPipelines{};
            std::array<VkPipeline, 4>               aoPipelines{};
            std::array<VkShaderModule, 4>           aoShaders{};
            std::vector<Buffer>                     buffers;
            std::vector<VkAccelerationStructureKHR> structures;
            ~Context()
            {
                if (device)
                {
                    api.vkDeviceWaitIdle(device);
                    for (auto p : aoPipelines)
                        if (p)
                            api.vkDestroyPipeline(device, p, nullptr);
                    for (auto m : aoShaders)
                        if (m)
                            api.vkDestroyShaderModule(device, m, nullptr);
                    for (auto p : mltPipelines)
                        if (p)
                            api.vkDestroyPipeline(device, p, nullptr);
                    for (auto m : mltShaders)
                        if (m)
                            api.vkDestroyShaderModule(device, m, nullptr);
                    for (auto p : photonPipelines)
                        if (p)
                            api.vkDestroyPipeline(device, p, nullptr);
                    for (auto m : photonShaders)
                        if (m)
                            api.vkDestroyShaderModule(device, m, nullptr);
                    if (pipeline)
                        api.vkDestroyPipeline(device, pipeline, nullptr);
                    if (denoisePipeline)
                        api.vkDestroyPipeline(device, denoisePipeline, nullptr);
                    if (denoiseShader)
                        api.vkDestroyShaderModule(device, denoiseShader, nullptr);
                    if (shader)
                        api.vkDestroyShaderModule(device, shader, nullptr);
                    if (pipelineLayout)
                        api.vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
                    if (descriptors)
                        api.vkDestroyDescriptorPool(device, descriptors, nullptr);
                    if (layout)
                        api.vkDestroyDescriptorSetLayout(device, layout, nullptr);
                    if (commands)
                        api.vkDestroyCommandPool(device, commands, nullptr);
                    for (auto as : structures)
                        api.vkDestroyAccelerationStructureKHR(device, as, nullptr);
                    for (auto b : buffers)
                    {
                        api.vkDestroyBuffer(device, b.handle, nullptr);
                        api.vkFreeMemory(device, b.memory, nullptr);
                    }
                    api.vkDestroyDevice(device, nullptr);
                }
                if (messenger)
                    api.vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
                if (instance)
                    api.vkDestroyInstance(instance, nullptr);
            }
            Buffer MakeBuffer(
              VkDeviceSize size, VkBufferUsageFlags usage, bool host = false, const void* initial = nullptr)
            {
                VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                info.size  = size;
                info.usage = usage;
                Buffer result;
                Check(api.vkCreateBuffer(device, &info, nullptr, &result.handle));
                VkMemoryRequirements req;
                api.vkGetBufferMemoryRequirements(device, result.handle, &req);
                VkPhysicalDeviceMemoryProperties memory;
                api.vkGetPhysicalDeviceMemoryProperties(physical, &memory);
                const VkMemoryPropertyFlags flags =
                  host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                       : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                uint32_t type = 0;
                for (; type < memory.memoryTypeCount; ++type)
                    if (
                      (req.memoryTypeBits & (1u << type)) && (memory.memoryTypes[type].propertyFlags & flags) == flags)
                        break;
                if (type == memory.memoryTypeCount)
                    throw std::runtime_error("Required memory type unavailable");
                VkMemoryAllocateFlagsInfo address{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
                address.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
                VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                alloc.allocationSize  = req.size;
                alloc.memoryTypeIndex = type;
                if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                    alloc.pNext = &address;
                Check(api.vkAllocateMemory(device, &alloc, nullptr, &result.memory));
                Check(api.vkBindBufferMemory(device, result.handle, result.memory, 0));
                if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                {
                    VkBufferDeviceAddressInfo a{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
                    a.buffer       = result.handle;
                    result.address = api.vkGetBufferDeviceAddress(device, &a);
                }
                buffers.push_back(result);
                if (initial)
                {
                    void* data;
                    Check(api.vkMapMemory(device, result.memory, 0, size, 0, &data));
                    std::memcpy(data, initial, size);
                    api.vkUnmapMemory(device, result.memory);
                }
                return result;
            }
            VkAccelerationStructureKHR MakeAS(VkAccelerationStructureTypeKHR type, VkDeviceSize size)
            {
                auto buffer = MakeBuffer(
                  size,
                  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
                VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
                info.type   = type;
                info.size   = size;
                info.buffer = buffer.handle;
                VkAccelerationStructureKHR result;
                Check(api.vkCreateAccelerationStructureKHR(device, &info, nullptr, &result));
                structures.push_back(result);
                return result;
            }
        };
        bool Has(const std::vector<VkExtensionProperties>& extensions, const char* name)
        {
            return std::any_of(extensions.begin(), extensions.end(), [&](auto& item) {
                return std::strcmp(item.extensionName, name) == 0;
            });
        }
    } // namespace

    namespace
    {
        class VulkanPath final : public PathDevice
        {
            bool                       software = false;
            Buffer                     bvhBuffer{};
            uint32_t                   bvhCount = 0;
            RendererConfig             config;
            std::unique_ptr<Context>   context;
            VkCommandBuffer            cmd{};
            VkAccelerationStructureKHR tlas{};
            VkDescriptorSet            set{};
            std::array<Buffer, 7>      data{};
            uint32_t                   width = 0, height = 0;
            size_t                     scratchBytes = 0;
            void                       Init()
            {
                context          = std::make_unique<Context>();
                auto& c          = *context;
                validationErrors = 0;
                if (!vkGetInstanceProcAddr || !vkCreateInstance)
                    Check(volkInitialize());

                uint32_t count = 0;
                Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
                std::vector<VkExtensionProperties> extensions(count);
                Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()));
                const char*       portability = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
                VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
                app.apiVersion = VK_API_VERSION_1_2;
                VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
                instance.pApplicationInfo = &app;
                std::vector<const char*> instanceExtensions;
                if (Has(extensions, portability))
                {
                    instance.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
                    instanceExtensions.push_back(portability);
                }
                if (Has(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
                    instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                instance.enabledExtensionCount   = static_cast<uint32_t>(instanceExtensions.size());
                instance.ppEnabledExtensionNames = instanceExtensions.data();
                uint32_t layerCount              = 0;
                Check(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));
                std::vector<VkLayerProperties> layers(layerCount);
                Check(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()));
                const char* validation = "VK_LAYER_KHRONOS_validation";
                const bool  hasValidation =
                  config.enableValidation && std::any_of(layers.begin(), layers.end(), [&](auto& item) {
                      return std::strcmp(item.layerName, validation) == 0;
                  });
                if (hasValidation)
                {
                    instance.enabledLayerCount   = 1;
                    instance.ppEnabledLayerNames = &validation;
                }

                Check(vkCreateInstance(&instance, nullptr, &c.instance));
                c.api.Load(c.instance);
                if (c.api.vkCreateDebugUtilsMessengerEXT)
                {
                    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
                    debug.messageSeverity =
                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
                    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                                        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                    debug.pfnUserCallback = Debug;
                    Check(c.api.vkCreateDebugUtilsMessengerEXT(c.instance, &debug, nullptr, &c.messenger));
                }
                Check(c.api.vkEnumeratePhysicalDevices(c.instance, &count, nullptr));
                std::vector<VkPhysicalDevice> devices(count);
                Check(c.api.vkEnumeratePhysicalDevices(c.instance, &count, devices.data()));
                bool needsVectorWorkaround = false;
                VkPhysicalDevice fallback              = VK_NULL_HANDLE;
                software                               = false;
                for (auto device : devices)
                {
                    VkPhysicalDeviceProperties props;
                    c.api.vkGetPhysicalDeviceProperties(device, &props);
                    uint32_t n = 0;
                    Check(c.api.vkEnumerateDeviceExtensionProperties(device, nullptr, &n, nullptr));
                    std::vector<VkExtensionProperties> es(n);
                    Check(c.api.vkEnumerateDeviceExtensionProperties(device, nullptr, &n, es.data()));
                    VkPhysicalDeviceRayQueryFeaturesKHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
                    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
                    as.pNext = &query;
                    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
                    v12.pNext = &as;
                    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
                    features.pNext = &v12;
                    c.api.vkGetPhysicalDeviceFeatures2(device, &features);
                    const bool supported = Has(es, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
                                           && Has(es, VK_KHR_RAY_QUERY_EXTENSION_NAME)
                                           && Has(es, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
                                           && as.accelerationStructure && query.rayQuery && v12.bufferDeviceAddress;
                    bool usable = supported;
                    if (!supported && !fallback && props.limits.maxPerStageDescriptorStorageBuffers >= 8)
                        fallback = device;
#if defined(__aarch64__)
                    const bool oldLavapipe = props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU && std::strstr(props.deviceName,"llvmpipe") &&
                        (VK_VERSION_MAJOR(props.driverVersion)<26 || (VK_VERSION_MAJOR(props.driverVersion)==26 && VK_VERSION_MINOR(props.driverVersion)<3));
                    const char* cache = std::getenv("MESA_SHADER_CACHE_DISABLE");
                    const bool  cacheDisabled =
                      cache && (std::strcmp(cache, "true") == 0 || std::strcmp(cache, "1") == 0);
                    if (oldLavapipe && (std::strstr(props.deviceName, "128 bits") || !cacheDisabled))
                    {
                        usable                = false;
                        if (fallback == device)
                            fallback = VK_NULL_HANDLE;
                        needsVectorWorkaround = true;
                    }
#endif
                    if (usable && !c.physical)
                        c.physical = device;
                }
                if (!c.physical && fallback)
                {
                    c.physical = fallback;
                    software   = true;
                }
                if (!c.physical)
                    throw std::runtime_error(
                      needsVectorWorkaround ? "Old ARM64 Lavapipe requires LP_NATIVE_VECTOR_WIDTH=256 and "
                                              "MESA_SHADER_CACHE_DISABLE=true, or newer Mesa"
                                            : "Vulkan driver does not expose required ray-query features");
                c.api.vkGetPhysicalDeviceQueueFamilyProperties(c.physical, &count, nullptr);
                std::vector<VkQueueFamilyProperties> families(count);
                c.api.vkGetPhysicalDeviceQueueFamilyProperties(c.physical, &count, families.data());
                uint32_t family = 0;
                for (; family < count; ++family)
                    if (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)
                        break;
                if (family == count)
                    throw std::runtime_error("No compute queue");
                const float             priority = 1;
                VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
                queue.queueFamilyIndex = family;
                queue.queueCount       = 1;
                queue.pQueuePriorities = &priority;
                VkPhysicalDeviceRayQueryFeaturesKHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
                query.rayQuery = VK_TRUE;
                VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
                as.accelerationStructure = VK_TRUE;
                as.pNext                 = &query;
                VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
                v12.bufferDeviceAddress = VK_TRUE;
                v12.pNext               = &as;
                const char* required[]  = {
                  VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                  VK_KHR_RAY_QUERY_EXTENSION_NAME,
                  VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
                VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
                device.pNext                   = software ? nullptr : &v12;
                device.queueCreateInfoCount    = 1;
                device.pQueueCreateInfos       = &queue;
                std::vector<const char*> enabled;
                if (!software)
                    enabled.assign(required, required + 3);
                uint32_t extensionCount = 0;
                c.api.vkEnumerateDeviceExtensionProperties(c.physical, nullptr, &extensionCount, nullptr);
                std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
                c.api.vkEnumerateDeviceExtensionProperties(
                  c.physical, nullptr, &extensionCount, deviceExtensions.data());
                if (Has(deviceExtensions, "VK_KHR_portability_subset"))
                    enabled.push_back("VK_KHR_portability_subset");
                device.enabledExtensionCount   = uint32_t(enabled.size());
                device.ppEnabledExtensionNames = enabled.data();
                Check(c.api.vkCreateDevice(c.physical, &device, nullptr, &c.device));

                c.api.vkGetDeviceQueue(c.device, family, 0, &c.queue);
                VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                pool.queueFamilyIndex = family;
                pool.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                Check(c.api.vkCreateCommandPool(c.device, &pool, nullptr, &c.commands));
                VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                alloc.commandPool        = c.commands;
                alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                alloc.commandBufferCount = 1;

                Check(c.api.vkAllocateCommandBuffers(c.device, &alloc, &cmd));

                VkPhysicalDeviceProperties props;
                c.api.vkGetPhysicalDeviceProperties(c.physical, &props);
                capabilities.pathTracing        = true;
                capabilities.softwareRayTracing = software || props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
                capabilities.rayTracingDevice   = props.deviceName;
                VkDescriptorSetLayoutBinding bindings[8]{};
                for (uint32_t i = 0; i < 8; ++i)
                {
                    bindings[i].binding         = i;
                    bindings[i].descriptorCount = 1;
                    bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
                    bindings[i].descriptorType  = (i || software) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                                                  : VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                }
                VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                layout.bindingCount = 8;
                layout.pBindings    = bindings;
                Check(c.api.vkCreateDescriptorSetLayout(c.device, &layout, nullptr, &c.layout));
                VkDescriptorPoolSize sizes[] = {
                  {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7}};
                VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                dp.maxSets       = 1;
                if (software)
                    sizes[1].descriptorCount = 8;
                dp.poolSizeCount = software ? 1 : 2;
                dp.pPoolSizes    = software ? &sizes[1] : sizes;
                Check(c.api.vkCreateDescriptorPool(c.device, &dp, nullptr, &c.descriptors));
                VkDescriptorSetAllocateInfo allocSet{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocSet.descriptorPool     = c.descriptors;
                allocSet.descriptorSetCount = 1;
                allocSet.pSetLayouts        = &c.layout;
                Check(c.api.vkAllocateDescriptorSets(c.device, &allocSet, &set));
                auto load = [&](const char* file) {
                    std::string name = file;
                    if (software)
                        name.insert(name.find(".compute.spv"), "-bvh");
                    return LoadShader(config.shaderDirectory, name.c_str());
                };
                const auto            bytes = load("pathtrace.compute.spv");
                std::vector<uint32_t> code(bytes.size() / 4);
                std::memcpy(code.data(), bytes.data(), bytes.size());
                VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                module.codeSize = bytes.size();
                module.pCode    = code.data();
                Check(c.api.vkCreateShaderModule(c.device, &module, nullptr, &c.shader));
                VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                pl.setLayoutCount = 1;
                pl.pSetLayouts    = &c.layout;
                Check(c.api.vkCreatePipelineLayout(c.device, &pl, nullptr, &c.pipelineLayout));
                VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                pipeline.layout       = c.pipelineLayout;
                pipeline.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                pipeline.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
                pipeline.stage.module = c.shader;
                pipeline.stage.pName  = "CSMain";
                Check(c.api.vkCreateComputePipelines(c.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &c.pipeline));
                const auto            filterBytes = load("denoise.compute.spv");
                std::vector<uint32_t> filterCode(filterBytes.size() / 4);
                std::memcpy(filterCode.data(), filterBytes.data(), filterBytes.size());
                module.codeSize = filterBytes.size();
                module.pCode    = filterCode.data();
                Check(c.api.vkCreateShaderModule(c.device, &module, nullptr, &c.denoiseShader));
                pipeline.stage.module = c.denoiseShader;
                pipeline.stage.pName  = "DenoiseMain";
                Check(
                  c.api.vkCreateComputePipelines(c.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &c.denoisePipeline));
                const char* files[]   = {"photon-clear.compute.spv", "photon-trace.compute.spv"};
                const char* entries[] = {"PhotonClear", "PhotonMain"};
                for (size_t i = 0; i < 2; i++)
                {
                    const auto            bytes = load(files[i]);
                    std::vector<uint32_t> code(bytes.size() / 4);
                    std::memcpy(code.data(), bytes.data(), bytes.size());
                    module.codeSize = bytes.size();
                    module.pCode    = code.data();
                    Check(c.api.vkCreateShaderModule(c.device, &module, nullptr, &c.photonShaders[i]));
                    pipeline.stage.module = c.photonShaders[i];
                    pipeline.stage.pName  = entries[i];
                    Check(c.api.vkCreateComputePipelines(
                      c.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &c.photonPipelines[i]));
                }
                const char* mltFiles[] = {
                  "mlt-bootstrap.compute.spv", "mlt-reduce.compute.spv", "mlt-mutate.compute.spv"};
                const char* mltEntries[] = {"MLTBootstrap", "MLTReduce", "MLTMutate"};
                for (size_t i = 0; i < 3; i++)
                {
                    const auto            bytes = load(mltFiles[i]);
                    std::vector<uint32_t> code(bytes.size() / 4);
                    std::memcpy(code.data(), bytes.data(), bytes.size());
                    module.codeSize = bytes.size();
                    module.pCode    = code.data();
                    Check(c.api.vkCreateShaderModule(c.device, &module, nullptr, &c.mltShaders[i]));
                    pipeline.stage.module = c.mltShaders[i];
                    pipeline.stage.pName  = mltEntries[i];
                    Check(c.api.vkCreateComputePipelines(
                      c.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &c.mltPipelines[i]));
                }
                const char* aoFiles[] = {
                  "ao-prepare.compute.spv", "ao-pyramid.compute.spv", "ao-main.compute.spv", "ao-filter.compute.spv"};
                const char* aoEntries[] = {"AOPrepare", "AOPyramid", "AOMain", "AOFilter"};
                for (size_t i = 0; i < 4; i++)
                {
                    const auto            bytes = load(aoFiles[i]);
                    std::vector<uint32_t> code(bytes.size() / 4);
                    std::memcpy(code.data(), bytes.data(), bytes.size());
                    module.codeSize = bytes.size();
                    module.pCode    = code.data();
                    Check(c.api.vkCreateShaderModule(c.device, &module, nullptr, &c.aoShaders[i]));
                    pipeline.stage.module = c.aoShaders[i];
                    pipeline.stage.pName  = aoEntries[i];
                    Check(c.api.vkCreateComputePipelines(
                      c.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &c.aoPipelines[i]));
                }
            }
            void Submit()
            {
                auto& c = *context;
                Check(c.api.vkEndCommandBuffer(cmd));
                if (validationErrors.load())
                    throw std::runtime_error("Vulkan path validation error");
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.commandBufferCount = 1;
                submit.pCommandBuffers    = &cmd;
                Check(c.api.vkQueueSubmit(c.queue, 1, &submit, VK_NULL_HANDLE));
                Check(c.api.vkQueueWaitIdle(c.queue));
                if (validationErrors.load())
                    throw std::runtime_error("Vulkan path validation error");
            }
            void ReleaseImageBuffer(Buffer& buffer)
            {
                if (!buffer.handle)
                    return;
                auto& c = *context;
                c.api.vkDestroyBuffer(c.device, buffer.handle, nullptr);
                c.api.vkFreeMemory(c.device, buffer.memory, nullptr);
                c.buffers.erase(
                  std::remove_if(
                    c.buffers.begin(), c.buffers.end(), [&](const Buffer& b) { return b.handle == buffer.handle; }),
                  c.buffers.end());
                buffer = {};
            }

          public:
            explicit VulkanPath(const RendererConfig& value)
              : config(value)
            {
                Init();
            }
            void SetScene(const PathScene& scene) override
            {
                auto& c = *context;
                Check(c.api.vkQueueWaitIdle(c.queue));
                Check(c.api.vkResetCommandBuffer(cmd, 0));
                for (auto structure : c.structures)
                    c.api.vkDestroyAccelerationStructureKHR(c.device, structure, nullptr);
                c.structures.clear();
                for (const auto& buffer : c.buffers)
                {
                    c.api.vkDestroyBuffer(c.device, buffer.handle, nullptr);
                    c.api.vkFreeMemory(c.device, buffer.memory, nullptr);
                }
                c.buffers.clear();
                tlas      = {};
                bvhBuffer = {};
                bvhCount  = 0;
                width = height = 0;
                data           = {};
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                Check(c.api.vkBeginCommandBuffer(cmd, &begin));
                data[0]     = c.MakeBuffer(sizeof(PathUniforms), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
                auto vertex = c.MakeBuffer(
                  scene.model.vertices.size() * sizeof(ModelVertex),
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | (software ? 0u : VkBufferUsageFlags(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)),
                  true,
                  scene.model.vertices.data());
                data[1] = vertex;
                data[2] =
                  c.MakeBuffer(scene.texels.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, scene.texels.data());
                data[3] = c.MakeBuffer(
                  scene.sky.pixels.size() * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, scene.sky.pixels.data());
                if (software)
                {
                    // Leaves retain original primitive indices, so emitter CDFs and material slots remain unchanged.
                    struct Node
                    {
                        float    low[3];
                        uint32_t escape;
                        float    high[3];
                        uint32_t first;
                        uint32_t count, pad[3];
                    };
                    static_assert(sizeof(Node) == 48, "Path BVH layout");
                    std::vector<uint32_t> triangles(scene.model.vertices.size() / 3);
                    std::iota(triangles.begin(), triangles.end(), 0u);
                    std::vector<Node>                   nodes;
                    std::function<void(size_t, size_t)> split = [&](size_t first, size_t end) {
                        Node n{};
                        for (int k = 0; k < 3; k++)
                        {
                            n.low[k]  = std::numeric_limits<float>::infinity();
                            n.high[k] = -n.low[k];
                        }
                        for (size_t i = first; i < end; i++)
                            for (int v = 0; v < 3; v++)
                                for (int k = 0; k < 3; k++)
                                {
                                    float p   = scene.model.vertices[triangles[i] * 3 + v].position[k];
                                    n.low[k]  = std::min(n.low[k], p);
                                    n.high[k] = std::max(n.high[k], p);
                                }
                        const auto index = nodes.size();
                        nodes.push_back(n);
                        if (end - first == 1)
                        {
                            nodes[index].first = triangles[first];
                            nodes[index].count = 1;
                        }
                        else
                        {
                            int axis = 0;
                            for (int k = 1; k < 3; k++)
                                if (n.high[k] - n.low[k] > n.high[axis] - n.low[axis])
                                    axis = k;
                            size_t middle = (first + end) / 2;
                            auto   center = [&](uint32_t t) {
                                float c = 0;
                                for (int v = 0; v < 3; v++)
                                    c += scene.model.vertices[t * 3 + v].position[axis];
                                return c;
                            };
                            std::nth_element(
                              triangles.begin() + first,
                              triangles.begin() + middle,
                              triangles.begin() + end,
                              [&](uint32_t a, uint32_t b) { return center(a) < center(b); });
                            split(first, middle);
                            split(middle, end);
                        }
                        nodes[index].escape = uint32_t(nodes.size());
                    };
                    split(0, triangles.size());
                    bvhCount = uint32_t(nodes.size());
                    bvhBuffer =
                      c.MakeBuffer(nodes.size() * sizeof(Node), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, nodes.data());
                }
                else
                {
                    std::vector<uint32_t> indices(scene.model.vertices.size());
                    std::iota(indices.begin(), indices.end(), 0u);
                    auto index = c.MakeBuffer(
                      indices.size() * 4,
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      true,
                      indices.data());
                    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
                    geometry.geometryType  = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                    geometry.flags         = VK_GEOMETRY_OPAQUE_BIT_KHR;
                    auto& triangles        = geometry.geometry.triangles;
                    triangles.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                    triangles.vertexStride = sizeof(ModelVertex);
                    triangles.maxVertex    = uint32_t(scene.model.vertices.size() - 1);
                    triangles.vertexData.deviceAddress = vertex.address;
                    triangles.indexType                = VK_INDEX_TYPE_UINT32;
                    triangles.indexData.deviceAddress  = index.address;
                    const uint32_t primitiveCount      = uint32_t(scene.model.vertices.size() / 3);
                    auto           sizes               = [&](VkAccelerationStructureBuildGeometryInfoKHR& build) {
                        VkAccelerationStructureBuildSizesInfoKHR result{
                          VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
                        const uint32_t count =
                          build.type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR ? 1 : primitiveCount;
                        c.api.vkGetAccelerationStructureBuildSizesKHR(
                          c.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &count, &result);
                        return result;
                    };
                    VkAccelerationStructureBuildGeometryInfoKHR bottom{
                      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
                    bottom.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                    bottom.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
                    bottom.geometryCount = 1;
                    bottom.pGeometries   = &geometry;
                    auto bottomSize      = sizes(bottom);
                    auto blas            = c.MakeAS(bottom.type, bottomSize.accelerationStructureSize);
                    VkAccelerationStructureDeviceAddressInfoKHR address{
                      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
                    address.accelerationStructure = blas;
                    VkAccelerationStructureInstanceKHR instanceData{};
                    instanceData.transform.matrix[0][0]   = instanceData.transform.matrix[1][1] =
                      instanceData.transform.matrix[2][2] = 1;
                    instanceData.mask                     = 255;
                    instanceData.accelerationStructureReference =
                      c.api.vkGetAccelerationStructureDeviceAddressKHR(c.device, &address);
                    auto instances = c.MakeBuffer(
                      sizeof(instanceData),
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      true,
                      &instanceData);
                    VkAccelerationStructureGeometryKHR instanceGeometry{
                      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
                    instanceGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
                    instanceGeometry.geometry.instances.sType =
                      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
                    instanceGeometry.geometry.instances.data.deviceAddress = instances.address;
                    auto top                                               = bottom;
                    top.type        = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
                    top.pGeometries = &instanceGeometry;
                    auto topSize    = sizes(top);
                    tlas            = c.MakeAS(top.type, topSize.accelerationStructureSize);
                    VkPhysicalDeviceAccelerationStructurePropertiesKHR properties{
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
                    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
                    properties2.pNext = &properties;
                    c.api.vkGetPhysicalDeviceProperties2(c.physical, &properties2);
                    const VkDeviceSize alignment = properties.minAccelerationStructureScratchOffsetAlignment;
                    auto               scratch   = c.MakeBuffer(
                      std::max(bottomSize.buildScratchSize, topSize.buildScratchSize) + alignment,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
                    const auto scratchAddress = (scratch.address + alignment - 1) & ~(alignment - 1);
                    auto       barrier        = [&](VkPipelineStageFlags destination) {
                        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                        b.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                        b.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
                        if (destination & VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR)
                            b.dstAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                        c.api.vkCmdPipelineBarrier(
                          cmd,
                          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                          destination,
                          0,
                          1,
                          &b,
                          0,
                          nullptr,
                          0,
                          nullptr);
                    };
                    VkAccelerationStructureBuildRangeInfoKHR range{};
                    range.primitiveCount             = primitiveCount;
                    const auto* rangePtr             = &range;
                    bottom.dstAccelerationStructure  = blas;
                    bottom.scratchData.deviceAddress = scratchAddress;
                    c.api.vkCmdBuildAccelerationStructuresKHR(cmd, 1, &bottom, &rangePtr);
                    barrier(VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);

                    range.primitiveCount          = 1;
                    top.dstAccelerationStructure  = tlas;
                    top.scratchData.deviceAddress = scratchAddress;
                    c.api.vkCmdBuildAccelerationStructuresKHR(cmd, 1, &top, &rangePtr);
                    barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                }
                Submit();
            }
            std::vector<unsigned char> Trace(const PathUniforms& uniforms) override
            {
                auto& c = *context;
                if (
                  width != uniforms.frame[1] || height != uniforms.frame[2]
                  || scratchBytes != PathScratchBytes(uniforms))
                {
                    ReleaseImageBuffer(data[4]);
                    ReleaseImageBuffer(data[5]);
                    ReleaseImageBuffer(data[6]);
                    scratchBytes = PathScratchBytes(uniforms);
                    width        = uniforms.frame[1];
                    height       = uniforms.frame[2];
                    data[4]      = c.MakeBuffer(size_t(width) * height * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                    data[5]      = c.MakeBuffer(size_t(width) * height * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
                    data[6]      = c.MakeBuffer(scratchBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                }
                VkWriteDescriptorSetAccelerationStructureKHR writeAS{
                  VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
                writeAS.accelerationStructureCount = 1;
                writeAS.pAccelerationStructures    = &tlas;
                VkDescriptorBufferInfo infos[8]{};
                VkWriteDescriptorSet   writes[8]{};
                for (uint32_t i = 0; i < 8; ++i)
                {
                    auto& w           = writes[i];
                    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    w.dstSet          = set;
                    w.dstBinding      = i;
                    w.descriptorCount = 1;
                    w.descriptorType  = (i || software) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                                        : VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                    if (!i && !software)
                        w.pNext = &writeAS;
                    else
                    {
                        infos[i]      = {i ? data[i - 1].handle : bvhBuffer.handle, 0, VK_WHOLE_SIZE};
                        w.pBufferInfo = &infos[i];
                    }
                }
                c.api.vkUpdateDescriptorSets(c.device, 8, writes, 0, nullptr);
                void* mapped;
                Check(c.api.vkMapMemory(c.device, data[0].memory, 0, sizeof(uniforms), 0, &mapped));
                auto parameters = uniforms;
                if (software)
                    parameters.options[2] = bvhCount;
                std::memcpy(mapped, &parameters, sizeof(parameters));
                c.api.vkUnmapMemory(c.device, data[0].memory);
                Check(c.api.vkResetCommandBuffer(cmd, 0));
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                Check(c.api.vkBeginCommandBuffer(cmd, &begin));
                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                c.api.vkCmdPipelineBarrier(
                  cmd,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  0,
                  1,
                  &barrier,
                  0,
                  nullptr,
                  0,
                  nullptr);
                c.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipeline);
                c.api.vkCmdBindDescriptorSets(
                  cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipelineLayout, 0, 1, &set, 0, nullptr);
                if ((uniforms.options[3] & 64u) && !uniforms.frame[0])
                {
                    c.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.aoPipelines[0]);
                    c.api.vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
                    c.api.vkCmdPipelineBarrier(
                      cmd,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      0,
                      1,
                      &barrier,
                      0,
                      nullptr,
                      0,
                      nullptr);
                    uint32_t method;
                    std::memcpy(&method, &uniforms.clear[3], 4);
                    if (method == 3)
                    {
                        c.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.aoPipelines[1]);
                        c.api.vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
                        c.api.vkCmdPipelineBarrier(
                          cmd,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          0,
                          1,
                          &barrier,
                          0,
                          nullptr,
                          0,
                          nullptr);
                    }
                }
                c.api.vkCmdBindPipeline(
                  cmd, VK_PIPELINE_BIND_POINT_COMPUTE, (uniforms.options[3] & 64u) ? c.aoPipelines[2] : c.pipeline);
                if (uniforms.options[3] & 32u)
                {
                    uint32_t boot;
                    std::memcpy(&boot, &uniforms.clear[3], 4);
                    for (int stage = uniforms.frame[0] ? 2 : 0; stage < 3; stage++)
                    {
                        c.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.mltPipelines[stage]);
                        const uint32_t count = stage == 0   ? std::max(width * height, boot)
                                               : stage == 1 ? 1
                                                            : std::min(1024u, width * height);
                        c.api.vkCmdDispatch(
                          cmd,
                          stage == 0   ? std::min((count + 63) / 64, 1024u)
                          : stage == 1 ? 1
                                       : (count + 63) / 64,
                          stage == 0 ? (count + 65535) / 65536 : 1,
                          1);
                        c.api.vkCmdPipelineBarrier(
                          cmd,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          0,
                          1,
                          &barrier,
                          0,
                          nullptr,
                          0,
                          nullptr);
                    }
                    c.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipeline);
                }
                if (uniforms.options[3] & 16u)
                {
                    c.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.photonPipelines[0]);
                    c.api.vkCmdDispatch(cmd, 512, 1, 1);
                    c.api.vkCmdPipelineBarrier(
                      cmd,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      0,
                      1,
                      &barrier,
                      0,
                      nullptr,
                      0,
                      nullptr);
                    c.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.photonPipelines[1]);
                    c.api.vkCmdDispatch(cmd, (uniforms.images[2][3] + 63) / 64, 1, 1);
                    c.api.vkCmdPipelineBarrier(
                      cmd,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      0,
                      1,
                      &barrier,
                      0,
                      nullptr,
                      0,
                      nullptr);
                    c.api.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipeline);
                }
                c.api.vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
                if (uniforms.options[3] & 1u)
                {
                    c.api.vkCmdPipelineBarrier(
                      cmd,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      0,
                      1,
                      &barrier,
                      0,
                      nullptr,
                      0,
                      nullptr);
                    c.api.vkCmdBindPipeline(
                      cmd,
                      VK_PIPELINE_BIND_POINT_COMPUTE,
                      (uniforms.options[3] & 64u) ? c.aoPipelines[3] : c.denoisePipeline);
                    c.api.vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
                }
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                c.api.vkCmdPipelineBarrier(
                  cmd,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_PIPELINE_STAGE_HOST_BIT,
                  0,
                  1,
                  &barrier,
                  0,
                  nullptr,
                  0,
                  nullptr);
                Submit();
                std::vector<unsigned char> pixels(size_t(width) * height * 4);
                Check(c.api.vkMapMemory(c.device, data[5].memory, 0, pixels.size(), 0, &mapped));
                std::memcpy(pixels.data(), mapped, pixels.size());
                c.api.vkUnmapMemory(c.device, data[5].memory);
                return pixels;
            }
        };
    } // namespace
    std::unique_ptr<PathDevice> CreateVulkanPathDevice(const RendererConfig& config)
    {
        return std::make_unique<VulkanPath>(config);
    }
} // namespace render::detail
