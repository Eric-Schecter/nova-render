#include "AADevice.hpp"
#include "ShaderData.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <volk.h>
namespace render::detail
{
    namespace
    {
        void Check(VkResult r)
        {
            if (r != VK_SUCCESS)
                throw std::runtime_error("Vulkan AA call failed: " + std::to_string(r));
        }
        struct Texture
        {
            VkImage        image{};
            VkDeviceMemory memory{};
            VkImageView    view{};
            VkImageLayout  layout = VK_IMAGE_LAYOUT_UNDEFINED;
            uint32_t       width = 0, height = 0;
            bool           depth = false;
        };
        struct Buffer
        {
            VkBuffer       buffer{};
            VkDeviceMemory memory{};
        };
        class VulkanAA final : public AADevice
        {
            uint32_t                              validationErrors = 0;
            VkDebugUtilsMessengerEXT              messenger{};
            static VKAPI_ATTR VkBool32 VKAPI_CALL Debug(
              VkDebugUtilsMessageSeverityFlagBitsEXT severity,
              VkDebugUtilsMessageTypeFlagsEXT,
              const VkDebugUtilsMessengerCallbackDataEXT* data,
              void*                                       self)
            {
                if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                {
                    static_cast<VulkanAA*>(self)->validationErrors++;
                    std::cerr << data->pMessage << '\n';
                }
                return VK_FALSE;
            }
            VkInstance       instance{};
            VkPhysicalDevice physical{};
            VkDevice         device{};
            VolkDeviceTable  api{};
            struct InstanceDispatch
            {
                PFN_vkDestroyInstance                        vkDestroyInstance{};
                PFN_vkEnumeratePhysicalDevices               vkEnumeratePhysicalDevices{};
                PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties{};
                PFN_vkGetPhysicalDeviceProperties            vkGetPhysicalDeviceProperties{};
                PFN_vkGetPhysicalDeviceMemoryProperties      vkGetPhysicalDeviceMemoryProperties{};
                PFN_vkEnumerateDeviceExtensionProperties     vkEnumerateDeviceExtensionProperties{};
                PFN_vkCreateDevice                           vkCreateDevice{};
                void                                         Load(VkInstance instance)
                {
                    vkDestroyInstance =
                      reinterpret_cast<PFN_vkDestroyInstance>(vkGetInstanceProcAddr(instance, "vkDestroyInstance"));
                    vkEnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
                      vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
                    vkGetPhysicalDeviceQueueFamilyProperties =
                      reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
                        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
                    vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
                      vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"));
                    vkGetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
                      vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties"));
                    vkEnumerateDeviceExtensionProperties = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
                      vkGetInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties"));
                    vkCreateDevice =
                      reinterpret_cast<PFN_vkCreateDevice>(vkGetInstanceProcAddr(instance, "vkCreateDevice"));
                }
            } instanceApi;

            VkQueue                          queue{};
            VkPhysicalDeviceMemoryProperties memory{};
            uint32_t                         family = 0, width = 0, height = 0;
            bool                             supersampled = false;
            VkCommandPool                    pool{};
            VkCommandBuffer                  command{};
            VkDescriptorPool                 descriptorPool{};
            VkDescriptorSetLayout            setLayout{};
            VkPipelineLayout                 layout{};
            VkPipeline                       pipelines[9]{}, sceneMS{};
            VkRenderPass                     scenePass{}, msPass{}, postPass{};
            VkSampler                        linear{}, point{};
            Texture                          textures[AAResourceCount], depth, depthMS, depthBaseline;
            Buffer                           uniforms, readback;
            VkDeviceSize                     uniformStride = 256;
            uint32_t                         Type(uint32_t bits, VkMemoryPropertyFlags flags)
            {
                for (uint32_t i = 0; i < memory.memoryTypeCount; i++)
                    if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags)
                        return i;
                throw std::runtime_error("Vulkan AA memory type unavailable");
            }
            Buffer MakeBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage)
            {
                Buffer             b;
                VkBufferCreateInfo d{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                d.size  = bytes;
                d.usage = usage;
                Check(api.vkCreateBuffer(device, &d, nullptr, &b.buffer));
                VkMemoryRequirements m;
                api.vkGetBufferMemoryRequirements(device, b.buffer, &m);
                VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                a.allocationSize = m.size;
                a.memoryTypeIndex =
                  Type(m.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                Check(api.vkAllocateMemory(device, &a, nullptr, &b.memory));
                Check(api.vkBindBufferMemory(device, b.buffer, b.memory, 0));
                return b;
            }
            void Destroy(Buffer& b)
            {
                if (b.buffer)
                    api.vkDestroyBuffer(device, b.buffer, nullptr);
                if (b.memory)
                    api.vkFreeMemory(device, b.memory, nullptr);
                b = {};
            }
            void Destroy(Texture& t)
            {
                if (t.view)
                    api.vkDestroyImageView(device, t.view, nullptr);
                if (t.image)
                    api.vkDestroyImage(device, t.image, nullptr);
                if (t.memory)
                    api.vkFreeMemory(device, t.memory, nullptr);
                t = {};
            }
            Texture MakeTexture(
              uint32_t w, uint32_t h, VkFormat format, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT)
            {
                Texture t;
                t.width  = w;
                t.height = h;
                t.depth  = format == VK_FORMAT_D32_SFLOAT;
                VkImageCreateInfo d{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                d.imageType = VK_IMAGE_TYPE_2D;
                d.extent    = {w, h, 1};
                d.mipLevels = d.arrayLayers = 1;
                d.format                    = format;
                d.tiling                    = VK_IMAGE_TILING_OPTIMAL;
                d.samples                   = samples;
                d.usage                     = t.depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                                      : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                                      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                Check(api.vkCreateImage(device, &d, nullptr, &t.image));
                VkMemoryRequirements m;
                api.vkGetImageMemoryRequirements(device, t.image, &m);
                VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                a.allocationSize  = m.size;
                a.memoryTypeIndex = Type(m.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                Check(api.vkAllocateMemory(device, &a, nullptr, &t.memory));
                Check(api.vkBindImageMemory(device, t.image, t.memory, 0));
                VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                v.image            = t.image;
                v.viewType         = VK_IMAGE_VIEW_TYPE_2D;
                v.format           = format;
                v.subresourceRange = {t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                Check(api.vkCreateImageView(device, &v, nullptr, &t.view));
                return t;
            }
            void Begin()
            {
                Check(api.vkResetCommandPool(device, pool, 0));
                VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                Check(api.vkBeginCommandBuffer(command, &b));
            }
            void End()
            {
                Check(api.vkEndCommandBuffer(command));
                VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                s.commandBufferCount = 1;
                s.pCommandBuffers    = &command;
                Check(api.vkQueueSubmit(queue, 1, &s, VK_NULL_HANDLE));
                Check(api.vkQueueWaitIdle(queue));
            }
            void Transition(Texture& t, VkImageLayout next)
            {
                VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                b.oldLayout           = t.layout;
                b.newLayout           = next;
                b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image                                       = t.image;
                b.subresourceRange = {t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                b.srcAccessMask =
                  t.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                api.vkCmdPipelineBarrier(
                  command,
                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                  0,
                  0,
                  nullptr,
                  0,
                  nullptr,
                  1,
                  &b);
                t.layout = next;
            }
            void Clear(Texture& t)
            {
                Transition(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkClearColorValue       c{};
                VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                api.vkCmdClearColorImage(command, t.image, t.layout, &c, 1, &range);
                Transition(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            VkRenderPass MakePass(bool scene, bool ms)
            {
                const uint32_t                       n = scene ? 2 : 1;
                std::vector<VkAttachmentDescription> attachments;
                for (uint32_t i = 0; i < n; i++)
                {
                    VkAttachmentDescription a{};
                    a.format         = VK_FORMAT_R16G16B16A16_SFLOAT;
                    a.samples        = ms ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
                    a.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    a.storeOp        = ms ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
                    a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    attachments.push_back(a);
                }
                if (ms)
                    for (uint32_t i = 0; i < n; i++)
                    {
                        auto a    = attachments[i];
                        a.samples = VK_SAMPLE_COUNT_1_BIT;
                        a.loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                        attachments.push_back(a);
                    }
                if (scene)
                {
                    VkAttachmentDescription a{};
                    a.format         = VK_FORMAT_D32_SFLOAT;
                    a.samples        = ms ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
                    a.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    a.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                    a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    attachments.push_back(a);
                }
                VkAttachmentReference colors[2] =
                  {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},
                                      resolves[2] =
                                        {{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                         {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},
                                      depthRef{
                                        uint32_t(attachments.size() - 1),
                                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
                VkSubpassDescription sub{};
                sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
                sub.colorAttachmentCount    = n;
                sub.pColorAttachments       = colors;
                sub.pResolveAttachments     = ms ? resolves : nullptr;
                sub.pDepthStencilAttachment = scene ? &depthRef : nullptr;
                VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
                info.attachmentCount = uint32_t(attachments.size());
                info.pAttachments    = attachments.data();
                info.subpassCount    = 1;
                info.pSubpasses      = &sub;
                VkRenderPass pass;
                Check(api.vkCreateRenderPass(device, &info, nullptr, &pass));
                return pass;
            }
            VkPipeline Pipeline(int program, bool ms, const RendererConfig& config)
            {
                VkShaderModule                  modules[2];
                VkPipelineShaderStageCreateInfo stages[2]{};
                for (int i = 0; i < 2; i++)
                {
                    const auto data = LoadShader(
                      config.shaderDirectory,
                      ("aa-" + std::to_string(program) + (i ? ".frag.spv" : ".vert.spv")).c_str());
                    VkShaderModuleCreateInfo d{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                    d.codeSize = data.size();
                    d.pCode    = reinterpret_cast<const uint32_t*>(data.data());
                    Check(api.vkCreateShaderModule(device, &d, nullptr, &modules[i]));
                    stages[i]        = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                    stages[i].stage  = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
                    stages[i].module = modules[i];
                    stages[i].pName  = i ? "PSMain" : "VSMain";
                }
                VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
                VkPipelineInputAssemblyStateCreateInfo assembly{
                  VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
                assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
                vp.viewportCount = vp.scissorCount = 1;
                VkPipelineRasterizationStateCreateInfo raster{
                  VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
                raster.polygonMode = VK_POLYGON_MODE_FILL;
                raster.lineWidth   = 1;
                raster.cullMode    = VK_CULL_MODE_NONE;
                VkPipelineMultisampleStateCreateInfo sampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
                sampling.rasterizationSamples = ms ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
                VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
                ds.depthTestEnable = ds.depthWriteEnable = program == 0;
                ds.depthCompareOp                        = VK_COMPARE_OP_LESS;
                VkPipelineColorBlendAttachmentState blends[2]{};
                blends[0].colorWriteMask = blends[1].colorWriteMask = 15;
                VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
                blend.attachmentCount                       = program == 0 ? 2 : 1;
                blend.pAttachments                          = blends;
                VkDynamicState                   dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
                VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
                dynamic.dynamicStateCount = 2;
                dynamic.pDynamicStates    = dynamics;
                VkGraphicsPipelineCreateInfo d{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                d.stageCount          = 2;
                d.pStages             = stages;
                d.pVertexInputState   = &vertex;
                d.pInputAssemblyState = &assembly;
                d.pViewportState      = &vp;
                d.pRasterizationState = &raster;
                d.pMultisampleState   = &sampling;
                d.pDepthStencilState  = &ds;
                d.pColorBlendState    = &blend;
                d.pDynamicState       = &dynamic;
                d.layout              = layout;
                d.renderPass          = program == 0 ? (ms ? msPass : scenePass) : postPass;
                VkPipeline result;
                auto       status = api.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &d, nullptr, &result);
                for (auto module : modules)
                    api.vkDestroyShaderModule(device, module, nullptr);
                Check(status);
                return result;
            }

          public:
            explicit VulkanAA(const RendererConfig& config)
            {
                try
                {
                    Initialize(config);
                }
                catch (...)
                {
                    Cleanup();
                    throw;
                }
            }
            void Initialize(const RendererConfig& config)
            {
                Check(volkInitialize());
                std::vector<const char*> extensions;
                uint32_t                 count = 0;
                Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
                std::vector<VkExtensionProperties> ext(count);
                Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, ext.data()));
                bool portability = std::any_of(ext.begin(), ext.end(), [](const auto& e) {
                    return std::strcmp(e.extensionName, "VK_KHR_portability_enumeration") == 0;
                });
                if (portability)
                    extensions.push_back("VK_KHR_portability_enumeration");
                uint32_t layerCount = 0;
                Check(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));
                std::vector<VkLayerProperties> layers(layerCount);
                Check(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()));
                const bool validation =
                  config.enableValidation
                  && std::any_of(
                    layers.begin(),
                    layers.end(),
                    [](const auto& l) { return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0; })
                  && std::any_of(ext.begin(), ext.end(), [](const auto& e) {
                         return std::strcmp(e.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
                     });
                if (validation)
                    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                const char*       layer = "VK_LAYER_KHRONOS_validation";
                VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
                app.apiVersion       = VK_API_VERSION_1_1;
                app.pApplicationName = "Nova AA";
                VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
                info.pApplicationInfo = &app;
                if (validation)
                {
                    info.enabledLayerCount   = 1;
                    info.ppEnabledLayerNames = &layer;
                }
                info.enabledExtensionCount   = uint32_t(extensions.size());
                info.ppEnabledExtensionNames = extensions.data();
                if (portability)
                    info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
                Check(vkCreateInstance(&info, nullptr, &instance));
                instanceApi.Load(instance);
                if (validation)
                {
                    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
                    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                    debug.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                                        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                    debug.pfnUserCallback = Debug;
                    debug.pUserData       = this;
                    auto create           = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                      vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
                    Check(create(instance, &debug, nullptr, &messenger));
                }
                Check(instanceApi.vkEnumeratePhysicalDevices(instance, &count, nullptr));
                std::vector<VkPhysicalDevice> devices(count);
                Check(instanceApi.vkEnumeratePhysicalDevices(instance, &count, devices.data()));
                for (auto candidate : devices)
                {
                    uint32_t n = 0;
                    instanceApi.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
                    std::vector<VkQueueFamilyProperties> q(n);
                    instanceApi.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, q.data());
                    for (uint32_t f = 0; f < n; f++)
                        if (q[f].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                        {
                            physical = candidate;
                            family   = f;
                            break;
                        }
                    if (physical)
                        break;
                }
                if (!physical)
                    throw std::runtime_error("Vulkan AA graphics device unavailable");
                VkPhysicalDeviceProperties props;
                instanceApi.vkGetPhysicalDeviceProperties(physical, &props);
                if (!(props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts
                      & VK_SAMPLE_COUNT_4_BIT))
                    throw std::runtime_error("Vulkan AA requires 4x MSAA");
                uniformStride = std::max(VkDeviceSize(256), props.limits.minUniformBufferOffsetAlignment);
                instanceApi.vkGetPhysicalDeviceMemoryProperties(physical, &memory);
                float                   priority = 1;
                VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
                qi.queueFamilyIndex = family;
                qi.queueCount       = 1;
                qi.pQueuePriorities = &priority;
                extensions.clear();
                Check(instanceApi.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr));
                ext.resize(count);
                Check(instanceApi.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, ext.data()));
                for (const auto& e : ext)
                    if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0)
                        extensions.push_back("VK_KHR_portability_subset");
                VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
                di.queueCreateInfoCount    = 1;
                di.pQueueCreateInfos       = &qi;
                di.enabledExtensionCount   = uint32_t(extensions.size());
                di.ppEnabledExtensionNames = extensions.data();
                Check(instanceApi.vkCreateDevice(physical, &di, nullptr, &device));
                // Never overwrite the window backend's global Volk dispatch.
                const auto getDeviceProc =
                  reinterpret_cast<PFN_vkGetDeviceProcAddr>(vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
                api.vkAllocateCommandBuffers =
                  reinterpret_cast<PFN_vkAllocateCommandBuffers>(getDeviceProc(device, "vkAllocateCommandBuffers"));
                api.vkAllocateDescriptorSets =
                  reinterpret_cast<PFN_vkAllocateDescriptorSets>(getDeviceProc(device, "vkAllocateDescriptorSets"));
                api.vkAllocateMemory =
                  reinterpret_cast<PFN_vkAllocateMemory>(getDeviceProc(device, "vkAllocateMemory"));
                api.vkBeginCommandBuffer =
                  reinterpret_cast<PFN_vkBeginCommandBuffer>(getDeviceProc(device, "vkBeginCommandBuffer"));
                api.vkBindBufferMemory =
                  reinterpret_cast<PFN_vkBindBufferMemory>(getDeviceProc(device, "vkBindBufferMemory"));
                api.vkBindImageMemory =
                  reinterpret_cast<PFN_vkBindImageMemory>(getDeviceProc(device, "vkBindImageMemory"));
                api.vkCmdBeginRenderPass =
                  reinterpret_cast<PFN_vkCmdBeginRenderPass>(getDeviceProc(device, "vkCmdBeginRenderPass"));
                api.vkCmdBindDescriptorSets =
                  reinterpret_cast<PFN_vkCmdBindDescriptorSets>(getDeviceProc(device, "vkCmdBindDescriptorSets"));
                api.vkCmdBindPipeline =
                  reinterpret_cast<PFN_vkCmdBindPipeline>(getDeviceProc(device, "vkCmdBindPipeline"));
                api.vkCmdClearColorImage =
                  reinterpret_cast<PFN_vkCmdClearColorImage>(getDeviceProc(device, "vkCmdClearColorImage"));
                api.vkCmdCopyBufferToImage =
                  reinterpret_cast<PFN_vkCmdCopyBufferToImage>(getDeviceProc(device, "vkCmdCopyBufferToImage"));
                api.vkCmdCopyImageToBuffer =
                  reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(getDeviceProc(device, "vkCmdCopyImageToBuffer"));
                api.vkCmdDraw = reinterpret_cast<PFN_vkCmdDraw>(getDeviceProc(device, "vkCmdDraw"));
                api.vkCmdEndRenderPass =
                  reinterpret_cast<PFN_vkCmdEndRenderPass>(getDeviceProc(device, "vkCmdEndRenderPass"));
                api.vkCmdPipelineBarrier =
                  reinterpret_cast<PFN_vkCmdPipelineBarrier>(getDeviceProc(device, "vkCmdPipelineBarrier"));
                api.vkCmdSetScissor = reinterpret_cast<PFN_vkCmdSetScissor>(getDeviceProc(device, "vkCmdSetScissor"));
                api.vkCmdSetViewport =
                  reinterpret_cast<PFN_vkCmdSetViewport>(getDeviceProc(device, "vkCmdSetViewport"));
                api.vkCreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(getDeviceProc(device, "vkCreateBuffer"));
                api.vkCreateCommandPool =
                  reinterpret_cast<PFN_vkCreateCommandPool>(getDeviceProc(device, "vkCreateCommandPool"));
                api.vkCreateDescriptorPool =
                  reinterpret_cast<PFN_vkCreateDescriptorPool>(getDeviceProc(device, "vkCreateDescriptorPool"));
                api.vkCreateDescriptorSetLayout = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(
                  getDeviceProc(device, "vkCreateDescriptorSetLayout"));
                api.vkCreateFramebuffer =
                  reinterpret_cast<PFN_vkCreateFramebuffer>(getDeviceProc(device, "vkCreateFramebuffer"));
                api.vkCreateGraphicsPipelines =
                  reinterpret_cast<PFN_vkCreateGraphicsPipelines>(getDeviceProc(device, "vkCreateGraphicsPipelines"));
                api.vkCreateImage = reinterpret_cast<PFN_vkCreateImage>(getDeviceProc(device, "vkCreateImage"));
                api.vkCreateImageView =
                  reinterpret_cast<PFN_vkCreateImageView>(getDeviceProc(device, "vkCreateImageView"));
                api.vkCreatePipelineLayout =
                  reinterpret_cast<PFN_vkCreatePipelineLayout>(getDeviceProc(device, "vkCreatePipelineLayout"));
                api.vkCreateRenderPass =
                  reinterpret_cast<PFN_vkCreateRenderPass>(getDeviceProc(device, "vkCreateRenderPass"));
                api.vkCreateSampler = reinterpret_cast<PFN_vkCreateSampler>(getDeviceProc(device, "vkCreateSampler"));
                api.vkCreateShaderModule =
                  reinterpret_cast<PFN_vkCreateShaderModule>(getDeviceProc(device, "vkCreateShaderModule"));
                api.vkDestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(getDeviceProc(device, "vkDestroyBuffer"));
                api.vkDestroyCommandPool =
                  reinterpret_cast<PFN_vkDestroyCommandPool>(getDeviceProc(device, "vkDestroyCommandPool"));
                api.vkDestroyDescriptorPool =
                  reinterpret_cast<PFN_vkDestroyDescriptorPool>(getDeviceProc(device, "vkDestroyDescriptorPool"));
                api.vkDestroyDescriptorSetLayout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(
                  getDeviceProc(device, "vkDestroyDescriptorSetLayout"));
                api.vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(getDeviceProc(device, "vkDestroyDevice"));
                api.vkDestroyFramebuffer =
                  reinterpret_cast<PFN_vkDestroyFramebuffer>(getDeviceProc(device, "vkDestroyFramebuffer"));
                api.vkDestroyImage = reinterpret_cast<PFN_vkDestroyImage>(getDeviceProc(device, "vkDestroyImage"));
                api.vkDestroyImageView =
                  reinterpret_cast<PFN_vkDestroyImageView>(getDeviceProc(device, "vkDestroyImageView"));
                api.vkDestroyPipeline =
                  reinterpret_cast<PFN_vkDestroyPipeline>(getDeviceProc(device, "vkDestroyPipeline"));
                api.vkDestroyPipelineLayout =
                  reinterpret_cast<PFN_vkDestroyPipelineLayout>(getDeviceProc(device, "vkDestroyPipelineLayout"));
                api.vkDestroyRenderPass =
                  reinterpret_cast<PFN_vkDestroyRenderPass>(getDeviceProc(device, "vkDestroyRenderPass"));
                api.vkDestroySampler =
                  reinterpret_cast<PFN_vkDestroySampler>(getDeviceProc(device, "vkDestroySampler"));
                api.vkDestroyShaderModule =
                  reinterpret_cast<PFN_vkDestroyShaderModule>(getDeviceProc(device, "vkDestroyShaderModule"));
                api.vkDeviceWaitIdle =
                  reinterpret_cast<PFN_vkDeviceWaitIdle>(getDeviceProc(device, "vkDeviceWaitIdle"));
                api.vkEndCommandBuffer =
                  reinterpret_cast<PFN_vkEndCommandBuffer>(getDeviceProc(device, "vkEndCommandBuffer"));
                api.vkFreeMemory = reinterpret_cast<PFN_vkFreeMemory>(getDeviceProc(device, "vkFreeMemory"));
                api.vkGetBufferMemoryRequirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(
                  getDeviceProc(device, "vkGetBufferMemoryRequirements"));
                api.vkGetDeviceQueue =
                  reinterpret_cast<PFN_vkGetDeviceQueue>(getDeviceProc(device, "vkGetDeviceQueue"));
                api.vkGetImageMemoryRequirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(
                  getDeviceProc(device, "vkGetImageMemoryRequirements"));
                api.vkMapMemory     = reinterpret_cast<PFN_vkMapMemory>(getDeviceProc(device, "vkMapMemory"));
                api.vkQueueSubmit   = reinterpret_cast<PFN_vkQueueSubmit>(getDeviceProc(device, "vkQueueSubmit"));
                api.vkQueueWaitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(getDeviceProc(device, "vkQueueWaitIdle"));
                api.vkResetCommandPool =
                  reinterpret_cast<PFN_vkResetCommandPool>(getDeviceProc(device, "vkResetCommandPool"));
                api.vkResetDescriptorPool =
                  reinterpret_cast<PFN_vkResetDescriptorPool>(getDeviceProc(device, "vkResetDescriptorPool"));
                api.vkUnmapMemory = reinterpret_cast<PFN_vkUnmapMemory>(getDeviceProc(device, "vkUnmapMemory"));
                api.vkUpdateDescriptorSets =
                  reinterpret_cast<PFN_vkUpdateDescriptorSets>(getDeviceProc(device, "vkUpdateDescriptorSets"));

                api.vkGetDeviceQueue(device, family, 0, &queue);
                VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                pi.queueFamilyIndex = family;
                Check(api.vkCreateCommandPool(device, &pi, nullptr, &pool));
                VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                ca.commandPool        = pool;
                ca.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                ca.commandBufferCount = 1;
                Check(api.vkAllocateCommandBuffers(device, &ca, &command));
                std::array<VkDescriptorSetLayoutBinding, 11> bindings{};
                for (uint32_t i = 0; i < 11; i++)
                {
                    bindings[i].binding         = i;
                    bindings[i].descriptorCount = 1;
                    bindings[i].descriptorType  = i == 0  ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                  : i < 9 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                          : VK_DESCRIPTOR_TYPE_SAMPLER;
                    bindings[i].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                }
                VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                sl.bindingCount = 11;
                sl.pBindings    = bindings.data();
                Check(api.vkCreateDescriptorSetLayout(device, &sl, nullptr, &setLayout));
                VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                pl.setLayoutCount = 1;
                pl.pSetLayouts    = &setLayout;
                Check(api.vkCreatePipelineLayout(device, &pl, nullptr, &layout));
                VkDescriptorPoolSize sizes[] = {
                  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
                  {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128},
                  {VK_DESCRIPTOR_TYPE_SAMPLER, 32}};
                VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                dp.maxSets       = 16;
                dp.poolSizeCount = 3;
                dp.pPoolSizes    = sizes;
                Check(api.vkCreateDescriptorPool(device, &dp, nullptr, &descriptorPool));
                VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                si.magFilter = si.minFilter = VK_FILTER_LINEAR;
                si.mipmapMode               = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                Check(api.vkCreateSampler(device, &si, nullptr, &linear));
                si.magFilter = si.minFilter = VK_FILTER_NEAREST;
                Check(api.vkCreateSampler(device, &si, nullptr, &point));
                scenePass = MakePass(true, false);
                msPass    = MakePass(true, true);
                postPass  = MakePass(false, false);
                for (int i = 0; i < 9; i++)
                    pipelines[i] = Pipeline(i, false, config);
                sceneMS  = Pipeline(0, true, config);
                uniforms = MakeBuffer(uniformStride * 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
                for (int i = AAArea; i <= AASearch; i++)
                {
                    const auto data = AALookup(i == AAArea);
                    textures[i]     = MakeTexture(data.width, data.height, VK_FORMAT_R8G8B8A8_UNORM);
                    auto  staging   = MakeBuffer(data.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
                    void* mapped;
                    Check(api.vkMapMemory(device, staging.memory, 0, VK_WHOLE_SIZE, 0, &mapped));
                    std::memcpy(mapped, data.pixels.data(), data.pixels.size());
                    api.vkUnmapMemory(device, staging.memory);
                    Begin();
                    Transition(textures[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    VkBufferImageCopy copy{};
                    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    copy.imageExtent      = {data.width, data.height, 1};
                    api.vkCmdCopyBufferToImage(
                      command, staging.buffer, textures[i].image, textures[i].layout, 1, &copy);
                    Transition(textures[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    End();
                    Destroy(staging);
                }
            }
            ~VulkanAA() override { Cleanup(); }
            void Cleanup()
            {
                if (!device)
                {
                    if (messenger)
                    {
                        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                          vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
                        destroy(instance, messenger, nullptr);
                    }
                    if (instance)
                        instanceApi.vkDestroyInstance(instance, nullptr);
                    return;
                }
                api.vkDeviceWaitIdle(device);
                for (auto& t : textures)
                    Destroy(t);
                Destroy(depth);
                Destroy(depthMS);
                Destroy(depthBaseline);
                Destroy(uniforms);
                Destroy(readback);
                for (auto p : pipelines)
                    if (p)
                        api.vkDestroyPipeline(device, p, nullptr);
                if (sceneMS)
                    api.vkDestroyPipeline(device, sceneMS, nullptr);
                for (auto p : {scenePass, msPass, postPass})
                    if (p)
                        api.vkDestroyRenderPass(device, p, nullptr);
                for (auto s : {linear, point})
                    if (s)
                        api.vkDestroySampler(device, s, nullptr);
                if (descriptorPool)
                    api.vkDestroyDescriptorPool(device, descriptorPool, nullptr);
                if (layout)
                    api.vkDestroyPipelineLayout(device, layout, nullptr);
                if (setLayout)
                    api.vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
                if (pool)
                    api.vkDestroyCommandPool(device, pool, nullptr);
                api.vkDestroyDevice(device, nullptr);
                if (messenger)
                {
                    auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                      vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
                    destroy(instance, messenger, nullptr);
                }
                instanceApi.vkDestroyInstance(instance, nullptr);
            }
            void Resize(uint32_t w, uint32_t h, bool ss) override
            {
                if (w == width && h == height && ss == supersampled)
                    return;
                width        = w;
                height       = h;
                supersampled = ss;
                for (int i = 0; i < AAResourceCount; i++)
                    if (i != AAArea && i != AASearch)
                    {
                        Destroy(textures[i]);
                        bool big    = i == AAColor || i == AAMotion;
                        textures[i] = MakeTexture(
                          i == AADummy ? 1 : w * (ss && big ? 2 : 1),
                          i == AADummy ? 1 : h * (ss && big ? 2 : 1),
                          VK_FORMAT_R16G16B16A16_SFLOAT,
                          i == AAMultiColor || i == AAMultiMotion ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT);
                    }
                Destroy(depth);
                Destroy(depthMS);
                Destroy(depthBaseline);
                depth         = MakeTexture(w * (ss ? 2 : 1), h * (ss ? 2 : 1), VK_FORMAT_D32_SFLOAT);
                depthMS       = MakeTexture(w, h, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_4_BIT);
                depthBaseline = MakeTexture(w, h, VK_FORMAT_D32_SFLOAT);
                Destroy(readback);
                readback = MakeBuffer(size_t(w) * h * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            }
            Image Draw(const std::vector<AAPass>& passes) override
            {
                Check(api.vkResetDescriptorPool(device, descriptorPool, 0));
                void* mapped;
                Check(api.vkMapMemory(device, uniforms.memory, 0, VK_WHOLE_SIZE, 0, &mapped));
                for (size_t i = 0; i < passes.size(); i++)
                    std::memcpy(
                      static_cast<char*>(mapped) + i * uniformStride, passes[i].uniforms.data(), sizeof(AAUniforms));
                api.vkUnmapMemory(device, uniforms.memory);
                Begin();
                for (int i : {AAHistoryA, AAHistoryB, AAPrevMotion, AADummy})
                    if (passes.front().uniforms[3][0] < .5f || i == AADummy)
                        Clear(textures[i]);
                std::vector<VkFramebuffer> framebuffers;
                for (size_t index = 0; index < passes.size(); index++)
                {
                    const auto&              p      = passes[index];
                    bool                     scene  = p.program == 0;
                    auto&                    target = textures[p.target];
                    std::vector<VkImageView> views;
                    for (int a = 0; a < (scene ? 2 : 1); a++)
                    {
                        auto& t = textures[p.multisampled ? AAMultiColor + a : p.target + a];
                        Transition(t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                        views.push_back(t.view);
                    }
                    if (p.multisampled)
                        for (int a = 0; a < 2; a++)
                        {
                            auto& t = textures[p.target + a];
                            Transition(t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                            views.push_back(t.view);
                        }
                    if (scene)
                    {
                        auto& t = p.target == AABaseline ? depthBaseline : p.multisampled ? depthMS : depth;
                        Transition(t, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                        views.push_back(t.view);
                    }
                    for (int source : p.sources)
                        Transition(textures[source], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                    da.descriptorPool     = descriptorPool;
                    da.descriptorSetCount = 1;
                    da.pSetLayouts        = &setLayout;
                    VkDescriptorSet set;
                    Check(api.vkAllocateDescriptorSets(device, &da, &set));
                    VkDescriptorBufferInfo ub{uniforms.buffer, index * uniformStride, sizeof(AAUniforms)};
                    std::array<VkDescriptorImageInfo, 10> images{};
                    std::array<VkWriteDescriptorSet, 11>  writes{};
                    for (uint32_t b = 0; b < 11; b++)
                    {
                        auto& write           = writes[b];
                        write                 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                        write.dstSet          = set;
                        write.dstBinding      = b;
                        write.descriptorCount = 1;
                        write.descriptorType  = b == 0  ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                : b < 9 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                        : VK_DESCRIPTOR_TYPE_SAMPLER;
                        if (b == 0)
                            write.pBufferInfo = &ub;
                        else
                        {
                            auto& image = images[b - 1];
                            if (b < 9)
                            {
                                image.imageView   = textures[p.sources[b - 1]].view;
                                image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                            }
                            else
                                image.sampler = b == 9 ? linear : point;
                            write.pImageInfo = &image;
                        }
                    }
                    api.vkUpdateDescriptorSets(device, 11, writes.data(), 0, nullptr);
                    VkRenderPass            renderPass = scene ? (p.multisampled ? msPass : scenePass) : postPass;
                    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                    fb.renderPass      = renderPass;
                    fb.attachmentCount = uint32_t(views.size());
                    fb.pAttachments    = views.data();
                    fb.width           = target.width;
                    fb.height          = target.height;
                    fb.layers          = 1;
                    VkFramebuffer framebuffer;
                    Check(api.vkCreateFramebuffer(device, &fb, nullptr, &framebuffer));
                    framebuffers.push_back(framebuffer);
                    VkClearValue clears[5]{};
                    clears[0].color = {{.012f, .02f, .035f, .1f}};
                    clears[1].color = {{0, 0, 1, 1}};
                    if (scene)
                        clears[views.size() - 1].depthStencil = {1, 0};
                    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                    begin.renderPass        = renderPass;
                    begin.framebuffer       = framebuffer;
                    begin.renderArea.extent = {target.width, target.height};
                    begin.clearValueCount   = uint32_t(views.size());
                    begin.pClearValues      = clears;
                    api.vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
                    api.vkCmdBindPipeline(
                      command, VK_PIPELINE_BIND_POINT_GRAPHICS, p.multisampled ? sceneMS : pipelines[p.program]);
                    api.vkCmdBindDescriptorSets(
                      command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
                    VkViewport vp{0, 0, float(target.width), float(target.height), 0, 1};
                    VkRect2D   scissor{{0, 0}, {target.width, target.height}};
                    api.vkCmdSetViewport(command, 0, 1, &vp);
                    api.vkCmdSetScissor(command, 0, 1, &scissor);
                    api.vkCmdDraw(command, scene ? 79 * 6 : 3, 1, 0, 0);
                    api.vkCmdEndRenderPass(command);
                }
                Transition(textures[AAOutput], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent      = {width, height, 1};
                api.vkCmdCopyImageToBuffer(
                  command, textures[AAOutput].image, textures[AAOutput].layout, readback.buffer, 1, &copy);
                End();
                for (auto fb : framebuffers)
                    api.vkDestroyFramebuffer(device, fb, nullptr);
                if (validationErrors)
                    throw std::runtime_error("Vulkan AA validation errors: " + std::to_string(validationErrors));
                Check(api.vkMapMemory(device, readback.memory, 0, VK_WHOLE_SIZE, 0, &mapped));
                auto image = AAReadImage(static_cast<uint16_t*>(mapped), width, height, size_t(width) * 8);
                api.vkUnmapMemory(device, readback.memory);
                return image;
            }
        };
    } // namespace
    std::unique_ptr<AADevice> CreateVulkanAA(const RendererConfig& c)
    {
        return std::make_unique<VulkanAA>(c);
    }
} // namespace render::detail
