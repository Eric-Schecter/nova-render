#include "Result.hpp"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <string>
#include <vector>
#include <volk.h>
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
    struct Context
    {
        VkDebugUtilsMessengerEXT                messenger{};
        VkInstance                              instance{};
        VkDevice                                device{};
        VkPhysicalDevice                        physical{};
        VkQueue                                 queue{};
        VkCommandPool                           commands{};
        VkDescriptorPool                        descriptors{};
        VkDescriptorSetLayout                   layout{};
        VkPipelineLayout                        pipelineLayout{};
        VkPipeline                              pipeline{};
        VkShaderModule                          shader{};
        std::vector<Buffer>                     buffers;
        std::vector<VkAccelerationStructureKHR> structures;
        ~Context()
        {
            if (device)
            {
                vkDeviceWaitIdle(device);
                if (pipeline)
                    vkDestroyPipeline(device, pipeline, nullptr);
                if (shader)
                    vkDestroyShaderModule(device, shader, nullptr);
                if (pipelineLayout)
                    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
                if (descriptors)
                    vkDestroyDescriptorPool(device, descriptors, nullptr);
                if (layout)
                    vkDestroyDescriptorSetLayout(device, layout, nullptr);
                if (commands)
                    vkDestroyCommandPool(device, commands, nullptr);
                for (auto as : structures)
                    vkDestroyAccelerationStructureKHR(device, as, nullptr);
                for (auto b : buffers)
                {
                    vkDestroyBuffer(device, b.handle, nullptr);
                    vkFreeMemory(device, b.memory, nullptr);
                }
                vkDestroyDevice(device, nullptr);
            }
            if (messenger)
                vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
            if (instance)
                vkDestroyInstance(instance, nullptr);
        }
        Buffer MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host = false, const void* initial = nullptr)
        {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size  = size;
            info.usage = usage;
            Buffer result;
            Check(vkCreateBuffer(device, &info, nullptr, &result.handle));
            VkMemoryRequirements req;
            vkGetBufferMemoryRequirements(device, result.handle, &req);
            VkPhysicalDeviceMemoryProperties memory;
            vkGetPhysicalDeviceMemoryProperties(physical, &memory);
            const VkMemoryPropertyFlags flags =
              host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                   : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            uint32_t type = 0;
            for (; type < memory.memoryTypeCount; ++type)
                if ((req.memoryTypeBits & (1u << type)) && (memory.memoryTypes[type].propertyFlags & flags) == flags)
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
            Check(vkAllocateMemory(device, &alloc, nullptr, &result.memory));
            Check(vkBindBufferMemory(device, result.handle, result.memory, 0));
            if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            {
                VkBufferDeviceAddressInfo a{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
                a.buffer       = result.handle;
                result.address = vkGetBufferDeviceAddress(device, &a);
            }
            buffers.push_back(result);
            if (initial)
            {
                void* data;
                Check(vkMapMemory(device, result.memory, 0, size, 0, &data));
                std::memcpy(data, initial, size);
                vkUnmapMemory(device, result.memory);
            }
            return result;
        }
        VkAccelerationStructureKHR MakeAS(VkAccelerationStructureTypeKHR type, VkDeviceSize size)
        {
            auto buffer = MakeBuffer(
              size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
            VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
            info.type   = type;
            info.size   = size;
            info.buffer = buffer.handle;
            VkAccelerationStructureKHR result;
            Check(vkCreateAccelerationStructureKHR(device, &info, nullptr, &result));
            structures.push_back(result);
            return result;
        }
    };
    bool Has(const std::vector<VkExtensionProperties>& extensions, const char* name)
    {
        return std::any_of(
          extensions.begin(), extensions.end(), [&](auto& item) { return std::strcmp(item.extensionName, name) == 0; });
    }
} // namespace
int main(int argc, char** argv)
{
    try
    {
        std::cout << std::unitbuf;
        if (argc != 2)
            throw std::runtime_error("Expected SPIR-V path");
        Check(volkInitialize());
        Context  c;
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
        const char* validation    = "VK_LAYER_KHRONOS_validation";
        const bool  hasValidation = std::any_of(
          layers.begin(), layers.end(), [&](auto& item) { return std::strcmp(item.layerName, validation) == 0; });
        if (hasValidation)
        {
            instance.enabledLayerCount   = 1;
            instance.ppEnabledLayerNames = &validation;
        }
        std::cout << "Vulkan validation layer=" << hasValidation << std::endl;
        Check(vkCreateInstance(&instance, nullptr, &c.instance));
        volkLoadInstance(c.instance);
        if (vkCreateDebugUtilsMessengerEXT)
        {
            VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            debug.messageSeverity =
              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
            debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                                | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug.pfnUserCallback = Debug;
            Check(vkCreateDebugUtilsMessengerEXT(c.instance, &debug, nullptr, &c.messenger));
        }
        Check(vkEnumeratePhysicalDevices(c.instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        Check(vkEnumeratePhysicalDevices(c.instance, &count, devices.data()));
        for (auto device : devices)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            uint32_t n = 0;
            Check(vkEnumerateDeviceExtensionProperties(device, nullptr, &n, nullptr));
            std::vector<VkExtensionProperties> es(n);
            Check(vkEnumerateDeviceExtensionProperties(device, nullptr, &n, es.data()));
            VkPhysicalDeviceRayQueryFeaturesKHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
            VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
            as.pNext = &query;
            VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            v12.pNext = &as;
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &v12;
            vkGetPhysicalDeviceFeatures2(device, &features);
            const bool supported = Has(es, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
                                   && Has(es, VK_KHR_RAY_QUERY_EXTENSION_NAME)
                                   && Has(es, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
                                   && as.accelerationStructure && query.rayQuery && v12.bufferDeviceAddress;
            std::cout << "Vulkan device: " << props.deviceName
                      << "; software=" << (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
                      << "; accelerationStructure=" << as.accelerationStructure << "; rayQuery=" << query.rayQuery
                      << "; rayTracingPipeline=" << Has(es, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) << '\n';
            if (supported && !c.physical)
                c.physical = device;
        }
        if (!c.physical)
        {
            std::cout << "SKIP: required Vulkan ray-query features unavailable\n";
            return 77;
        }
        vkGetPhysicalDeviceQueueFamilyProperties(c.physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(c.physical, &count, families.data());
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
        device.pNext                   = &v12;
        device.queueCreateInfoCount    = 1;
        device.pQueueCreateInfos       = &queue;
        device.enabledExtensionCount   = 3;
        device.ppEnabledExtensionNames = required;
        Check(vkCreateDevice(c.physical, &device, nullptr, &c.device));
        volkLoadDevice(c.device);
        vkGetDeviceQueue(c.device, family, 0, &c.queue);
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = family;
        pool.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Check(vkCreateCommandPool(c.device, &pool, nullptr, &c.commands));
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool        = c.commands;
        alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd;
        Check(vkAllocateCommandBuffers(c.device, &alloc, &cmd));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(cmd, &begin));
        auto checkpoint = [&](const char* label) {
            if (validationErrors.load())
                throw std::runtime_error("Validation failed before AS checkpoint");
            Check(vkEndCommandBuffer(cmd));
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers    = &cmd;
            Check(vkQueueSubmit(c.queue, 1, &submit, VK_NULL_HANDLE));
            Check(vkQueueWaitIdle(c.queue));
            std::cout << label << std::endl;
            Check(vkResetCommandBuffer(cmd, 0));
            Check(vkBeginCommandBuffer(cmd, &begin));
        };
        const float vertices[] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
        auto        vertex     = c.MakeBuffer(
          sizeof(vertices),
          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
          true,
          vertices);
        const uint32_t indices[] = {0, 1, 2};
        auto           index     = c.MakeBuffer(
          sizeof(indices),
          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
          true,
          indices);
        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType              = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags                     = VK_GEOMETRY_OPAQUE_BIT_KHR;
        auto& triangles                    = geometry.geometry.triangles;
        triangles.sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexStride             = 12;
        triangles.maxVertex                = 2;
        triangles.vertexData.deviceAddress = vertex.address;
        triangles.indexType                = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress  = index.address;
        const uint32_t primitiveCount      = 1;
        auto           sizes               = [&](VkAccelerationStructureBuildGeometryInfoKHR& build) {
            VkAccelerationStructureBuildSizesInfoKHR result{
              VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
            vkGetAccelerationStructureBuildSizesKHR(
              c.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &primitiveCount, &result);
            return result;
        };
        VkAccelerationStructureBuildGeometryInfoKHR bottom{
          VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        bottom.type                                            = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        bottom.mode                                            = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        bottom.geometryCount                                   = 1;
        bottom.pGeometries                                     = &geometry;
        auto                                        bottomSize = sizes(bottom);
        auto                                        blas = c.MakeAS(bottom.type, bottomSize.accelerationStructureSize);
        VkAccelerationStructureDeviceAddressInfoKHR address{
          VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        address.accelerationStructure = blas;
        VkAccelerationStructureInstanceKHR instanceData{};
        instanceData.transform.matrix[0][0]         = instanceData.transform.matrix[1][1] =
          instanceData.transform.matrix[2][2]       = 1;
        instanceData.mask                           = 255;
        instanceData.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(c.device, &address);
        auto instances                              = c.MakeBuffer(
          sizeof(instanceData),
          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
          true,
          &instanceData);
        VkAccelerationStructureGeometryKHR instanceGeometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        instanceGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        instanceGeometry.geometry.instances.sType =
          VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instanceGeometry.geometry.instances.data.deviceAddress     = instances.address;
        auto top                                                   = bottom;
        top.type                                                   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        top.pGeometries                                            = &instanceGeometry;
        auto                                               topSize = sizes(top);
        auto                                               tlas = c.MakeAS(top.type, topSize.accelerationStructureSize);
        VkPhysicalDeviceAccelerationStructurePropertiesKHR properties{
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties2.pNext = &properties;
        vkGetPhysicalDeviceProperties2(c.physical, &properties2);
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
            vkCmdPipelineBarrier(
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
        range.primitiveCount             = 1;
        const auto* rangePtr             = &range;
        bottom.dstAccelerationStructure  = blas;
        bottom.scratchData.deviceAddress = scratchAddress;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &bottom, &rangePtr);
        barrier(VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR);
        checkpoint("BLAS built");
        top.dstAccelerationStructure  = tlas;
        top.scratchData.deviceAddress = scratchAddress;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &top, &rangePtr);
        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        checkpoint("TLAS built");
        auto output = c.MakeBuffer(probe::ResultBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        VkDescriptorSetLayoutBinding bindings[] = {
          {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
          {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = 2;
        layout.pBindings    = bindings;
        Check(vkCreateDescriptorSetLayout(c.device, &layout, nullptr, &c.layout));
        VkDescriptorPoolSize poolSizes[] = {
          {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets       = 1;
        dp.poolSizeCount = 2;
        dp.pPoolSizes    = poolSizes;
        Check(vkCreateDescriptorPool(c.device, &dp, nullptr, &c.descriptors));
        VkDescriptorSetAllocateInfo ds{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ds.descriptorPool     = c.descriptors;
        ds.descriptorSetCount = 1;
        ds.pSetLayouts        = &c.layout;
        VkDescriptorSet set;
        Check(vkAllocateDescriptorSets(c.device, &ds, &set));
        VkWriteDescriptorSetAccelerationStructureKHR writeAS{
          VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
        writeAS.accelerationStructureCount = 1;
        writeAS.pAccelerationStructures    = &tlas;
        VkDescriptorBufferInfo bufferInfo{output.handle, 0, probe::ResultBytes};
        VkWriteDescriptorSet   writes[2]{};
        for (int i = 0; i < 2; ++i)
        {
            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = set;
            writes[i].dstBinding      = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType  = bindings[i].descriptorType;
        }
        writes[0].pNext       = &writeAS;
        writes[1].pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(c.device, 2, writes, 0, nullptr);
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0 || file.tellg() % 4)
            throw std::runtime_error("Invalid SPIR-V file");
        const auto length = size_t(file.tellg());
        file.seekg(0);
        std::vector<uint32_t> code(length / 4);
        file.read(reinterpret_cast<char*>(code.data()), length);
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module.codeSize = length;
        module.pCode    = code.data();
        Check(vkCreateShaderModule(c.device, &module, nullptr, &c.shader));
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts    = &c.layout;
        Check(vkCreatePipelineLayout(c.device, &pl, nullptr, &c.pipelineLayout));
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.layout       = c.pipelineLayout;
        pipeline.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline.stage.module = c.shader;
        pipeline.stage.pName  = "CSMain";
        Check(vkCreateComputePipelines(c.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &c.pipeline));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c.pipelineLayout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(cmd, 1, 1, 1);
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(
          cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
        Check(vkEndCommandBuffer(cmd));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;
        if (validationErrors.load())
            throw std::runtime_error("Vulkan validation errors before dispatch");
        Check(vkQueueSubmit(c.queue, 1, &submit, VK_NULL_HANDLE));
        Check(vkQueueWaitIdle(c.queue));
        void* mapped;
        Check(vkMapMemory(c.device, output.memory, 0, probe::ResultBytes, 0, &mapped));
        probe::Check(static_cast<const uint32_t*>(mapped));
        vkUnmapMemory(c.device, output.memory);
        if (validationErrors.load())
            throw std::runtime_error("Vulkan validation errors");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
