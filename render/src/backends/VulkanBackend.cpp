#include "../Backend.hpp"
#include "../ShaderData.hpp"
#include <volk.h>

#ifdef NOVA_HAS_UI
#include <imgui_impl_vulkan.h>
#endif
#include "../VulkanPresentation.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

static void check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
}
static bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    return std::any_of(
      extensions.begin(), extensions.end(), [name](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
}
static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(
  VkDebugUtilsMessageSeverityFlagBitsEXT severity,
  VkDebugUtilsMessageTypeFlagsEXT,
  const VkDebugUtilsMessengerCallbackDataEXT* data,
  void*                                       user)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ++*static_cast<unsigned*>(user);
    std::cerr << "[Vulkan] " << data->pMessage << '\n';
    return VK_FALSE;
}

namespace render::detail
{
    struct VulkanBackend final : Backend
    {
        ~VulkanBackend() { cleanup(); }
        void initialize(PresentationWindow target, const RendererConfig& settings)
        {
            if (!target)
                throw std::invalid_argument("Renderer requires a window");
            window = target;
            config = settings;
            initInstance();
            surface = CreateVulkanSurface(window, instance);
#ifdef __ANDROID__
            surfaceWindow = window.NativeHandle();
#endif
            initDevice();
            initCommands();
        }
        void Resize(uint32_t newWidth, uint32_t newHeight) override
        {
            if (width == newWidth && height == newHeight)
                return;
            width   = newWidth;
            height  = newHeight;
            resized = true;
        }
        FrameResult DrawFrame() override { return draw(); }
        void        WaitIdle() override { check(vkDeviceWaitIdle(device), "Wait idle"); }
        uint32_t    ValidationErrorCount() const override { return validationErrors; }
#ifdef NOVA_HAS_UI
        bool uiRenderer = false;
#endif
        PresentationWindow         window;
        RendererConfig             config;
        uint32_t                   width = 0, height = 0;
        bool                       resized          = true;
        VkInstance                 instance         = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT   messenger        = VK_NULL_HANDLE;
        unsigned                   validationErrors = 0;
        VkSurfaceKHR               surface          = VK_NULL_HANDLE;
#ifdef __ANDROID__
        void*                      surfaceWindow    = nullptr;
        bool                       surfaceLost      = false;
#endif
        VkPhysicalDevice           physical         = VK_NULL_HANDLE;
        VkDevice                   device           = VK_NULL_HANDLE;
        uint32_t                   queueFamily      = 0;
        VkQueue                    queue            = VK_NULL_HANDLE;
        VkSwapchainKHR             swapchain        = VK_NULL_HANDLE;
        VkFormat                   format           = VK_FORMAT_UNDEFINED;
        VkExtent2D                 extent{};
        std::vector<VkImageView>   views;
        std::vector<VkFramebuffer> framebuffers;
        std::vector<VkSemaphore>   finished;
        VkRenderPass               renderPass  = VK_NULL_HANDLE;
        VkPipelineLayout           layout      = VK_NULL_HANDLE;
        VkPipeline                 pipeline    = VK_NULL_HANDLE;
        VkPipeline                 skyPipeline = VK_NULL_HANDLE;
        VkCommandPool              pool        = VK_NULL_HANDLE;
        VkCommandBuffer            cmd         = VK_NULL_HANDLE;
        VkSemaphore                acquired    = VK_NULL_HANDLE;
        VkFence                    fence       = VK_NULL_HANDLE;

#include "VulkanModel.inc"
        ModelBuffer aaPreviousUniform, aaPreviousPalette;
        size_t      aaPreviousPaletteBytes = 0;
        VkPipeline  aaModel[2]{}, aaSky[2]{}, aaTrianglePipeline[2]{}, aaMotion{}, aaSkyMotion{}, aaGBuffer{},
          aaDeferred{}, aaOit{}, aaHybridShadow{}, aaHybridReflection{}, aaProbeReflection{}, aaDeferredHalf{};
        VkPipeline AAPipeline(const char* name, bool sky, uint32_t samples, VkRenderPass targetPass)
        {
            const auto vertex   = shader((std::string(name) + ".vert.spv").c_str()),
                       fragment = shader((std::string(name) + ".frag.spv").c_str());
            VkPipelineShaderStageCreateInfo stages[2]{};
            for (int i = 0; i < 2; i++)
            {
                stages[i]        = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[i].stage  = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
                stages[i].module = i ? fragment : vertex;
                stages[i].pName  = std::string(name) == "model-gbuffer"    ? (i ? "GBufferPSMain" : "VSMain")
                                   : std::string(name) == "model-deferred" ? (i ? "DeferredPSMain" : "SkyVSMain")
                                   : i ? (std::string(name) == "model-motion" ? "MotionPSMain"
                                          : std::string(name) == "sky-motion" ? "SkyMotionPSMain"
                                          : sky                               ? "SkyPSMain"
                                                                              : "PSMain")
                                       : (std::string(name) == "model-motion" ? "MotionVSMain"
                                          : sky                               ? "SkyVSMain"
                                                                              : "VSMain");
            }
            if (std::string(name).find("deferred") != std::string::npos)
                stages[1].pName = "DeferredPSMain";
            if (std::string(name) == "model-oit")
                stages[1].pName = "OITPSMain";
            if (std::string(name) == "model-hybrid-shadow")
                stages[1].pName = "HybridShadowPSMain";
            const bool reflectionPass = std::string_view(name) == "model-hybrid-reflection"
                                     || std::string_view(name) == "model-probe-reflection";
            if (reflectionPass)
                stages[1].pName = "HybridReflectionPSMain";
            VkPipelineVertexInputStateCreateInfo   input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo assembly{
              VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.lineWidth   = 1;
            raster.cullMode    = VK_CULL_MODE_NONE;
            VkPipelineMultisampleStateCreateInfo sampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            sampling.rasterizationSamples = samples == 4 ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            depth.depthTestEnable  = !reflectionPass;
            depth.depthWriteEnable = !sky && std::string(name) != "model-oit";
            depth.depthCompareOp   = sky                                ? VK_COMPARE_OP_ALWAYS
                                     : std::string(name) == "model-oit" ? VK_COMPARE_OP_LESS_OR_EQUAL
                                                                        : VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState color[5]{};
            for (auto& c : color)
                c.colorWriteMask = 15;
            if (std::string(name) == "model-oit")
                for (int i = 0; i < 2; i++)
                {
                    auto& b               = color[i];
                    b.blendEnable         = VK_TRUE;
                    b.srcColorBlendFactor = b.srcAlphaBlendFactor = i == 0 ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
                    b.dstColorBlendFactor                         = b.dstAlphaBlendFactor =
                      i == 0 ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                    b.colorBlendOp = b.alphaBlendOp = VK_BLEND_OP_ADD;
                }
            VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blend.attachmentCount                     = std::string(name) == "model-gbuffer"         ? 5
                                                        : std::string(name) == "model-hybrid-shadow" ? 4
                                                        : std::string(name) == "model-oit"           ? 2
                                                                                                     : 1;
            blend.pAttachments    = color;
            VkDynamicState                   states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates    = states;
            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            info.stageCount          = 2;
            info.pStages             = stages;
            info.pVertexInputState   = &input;
            info.pInputAssemblyState = &assembly;
            info.pViewportState      = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState   = &sampling;
            info.pDepthStencilState  = &depth;
            info.pColorBlendState    = &blend;
            info.pDynamicState       = &dynamic;
            info.layout              = layout;
            info.renderPass          = targetPass;
            VkPipeline result;
            const auto status = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &result);
            vkDestroyShaderModule(device, vertex, nullptr);
            vkDestroyShaderModule(device, fragment, nullptr);
            check(status, "Create raster AA pipeline");
            return result;
        }
        VkPipeline probeDeferredPipeline = VK_NULL_HANDLE, probeDeferredHalf = VK_NULL_HANDLE;
        bool       probeFrame            = false;
        void       EncodeProbeUpdate(VkCommandBuffer command)
        {
            bool reset;
            probeFrame = PrepareProbes(reset);
            if (!probeFrame)
                return;
            if (!probeUpdatePipeline)
            {
                constexpr auto bytes = ProbeCount * ProbeBytes;
                modelBuffer(probeUniform, sizeof(ProbeConfig), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
                modelBuffer(
                  probeCurrent,
                  bytes,
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
                modelBuffer(
                  probePrevious, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
                VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                alloc.descriptorPool     = modelPool;
                alloc.descriptorSetCount = 1;
                alloc.pSetLayouts        = &probeSetLayout;
                check(vkAllocateDescriptorSets(device, &alloc, &probeReadSet), "Allocate probe sampling set");
                check(vkAllocateDescriptorSets(device, &alloc, &probeUpdateSet), "Allocate probe update set");
                alloc.pSetLayouts = &modelSetLayout;
                check(vkAllocateDescriptorSets(device, &alloc, &probeModelSet), "Allocate probe scene set");
                auto bind = [&](VkDescriptorSet set, const ModelBuffer& input) {
                    VkDescriptorBufferInfo buffers[]{
                      {probeUniform.buffer, 0, sizeof(ProbeConfig)},
                      {input.buffer, 0, VK_WHOLE_SIZE},
                      {probeCurrent.buffer, 0, VK_WHOLE_SIZE}};
                    VkWriteDescriptorSet writes[3]{};
                    for (uint32_t i = 0; i < 3; ++i)
                    {
                        auto& write           = writes[i];
                        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet          = set;
                        write.dstBinding      = 25 + i;
                        write.descriptorCount = 1;
                        write.descriptorType =
                          i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        write.pBufferInfo = &buffers[i];
                    }
                    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
                };
                bind(probeReadSet, probeCurrent);
                bind(probeUpdateSet, probePrevious);
                auto                        module = shader("model-probe-update.comp.spv");
                VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                info.stage        = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
                info.stage.module = module;
                info.stage.pName  = "HybridProbeUpdateCS";
                info.layout       = layout;
                const auto status =
                  vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &probeUpdatePipeline);
                vkDestroyShaderModule(device, module, nullptr);
                check(status, "Create probe update pipeline");
                reset = true;
            }
            auto upload = [&](ModelBuffer& buffer, const void* data, size_t size) {
                void* mapped;
                check(vkMapMemory(device, buffer.memory, 0, size, 0, &mapped), "Map probe input");
                std::memcpy(mapped, data, size);
                vkUnmapMemory(device, buffer.memory);
            };
            const uint32_t      bindings[] = {0, 2, 3, 4, 5, 6, 7, 8, 14, 28, 29, 30};
            VkCopyDescriptorSet copies[std::size(bindings)]{};
            for (size_t i = 0; i < std::size(bindings); ++i)
            {
                copies[i].sType      = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
                copies[i].srcSet     = modelSet;
                copies[i].dstSet     = probeModelSet;
                copies[i].srcBinding = copies[i].dstBinding = bindings[i];
                copies[i].descriptorCount                   = 1;
            }
            vkUpdateDescriptorSets(device, 0, nullptr, std::size(copies), copies);
            upload(modelUniform, &rasterAA.current, sizeof(SceneUniforms));
            upload(probeUniform, &probeConfig, sizeof(ProbeConfig));
            auto barrier = [&](
                             VkPipelineStageFlags before,
                             VkAccessFlags        source,
                             VkPipelineStageFlags after,
                             VkAccessFlags        destination) {
                VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                memory.srcAccessMask = source;
                memory.dstAccessMask = destination;
                vkCmdPipelineBarrier(command, before, after, 0, 1, &memory, 0, nullptr, 0, nullptr);
            };
            barrier(
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
            if (reset)
            {
                vkCmdFillBuffer(command, probeCurrent.buffer, 0, VK_WHOLE_SIZE, 0);
                barrier(
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT);
            }
            const bool         feedback = hybrid.probeSkyLighting && hybrid.probeMultiBounce && bool(environment);
            const VkDeviceSize offset   = feedback ? 0 : probeConfig.dimensionsFirst[3] * ProbeBytes;
            const VkDeviceSize bytes    = (feedback ? ProbeCount : probeConfig.count) * ProbeBytes;
            VkBufferCopy       copy{offset, offset, bytes};
            vkCmdCopyBuffer(command, probeCurrent.buffer, probePrevious.buffer, 1, &copy);
            barrier(
              VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, probeUpdatePipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &probeModelSet, 0, nullptr);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 2, 1, &probeUpdateSet, 0, nullptr);
            vkCmdDispatch(command, (probeConfig.count * 6 + 31) / 32, 1, 1);
            barrier(
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              VK_ACCESS_SHADER_WRITE_BIT,
              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              VK_ACCESS_SHADER_READ_BIT);
        }
        void PrepareRasterAAInputs()
        {
            const auto& current  = rasterAA.current;
            const auto& previous = rasterAA.frames ? rasterAA.previous : current;
            const auto& palette  = rasterAA.frames ? rasterAA.previousJoints : model->jointMatrices;
            if (!aaPreviousUniform.buffer)
                modelBuffer(aaPreviousUniform, sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            size_t bytes = std::max(size_t(64), palette.size() * sizeof(JointMatrix));
            if (bytes > aaPreviousPaletteBytes)
            {
                destroyModelBuffer(aaPreviousPalette);
                modelBuffer(aaPreviousPalette, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                aaPreviousPaletteBytes = bytes;
            }
            auto write = [&](ModelBuffer& buffer, const void* data, size_t size) {
                if (!size)
                    return;
                void* mapped;
                check(vkMapMemory(device, buffer.memory, 0, size, 0, &mapped), "Map raster AA input");
                std::memcpy(mapped, data, size);
                vkUnmapMemory(device, buffer.memory);
            };
            write(modelUniform, &current, sizeof(current));
            write(aaPreviousUniform, &previous, sizeof(previous));
            write(aaPreviousPalette, palette.data(), palette.size() * sizeof(JointMatrix));
            VkDescriptorBufferInfo buffers[] = {
              {aaPreviousUniform.buffer, 0, sizeof(SceneUniforms)}, {aaPreviousPalette.buffer, 0, VK_WHOLE_SIZE}};
            VkWriteDescriptorSet writes[2]{};
            for (int i = 0; i < 2; i++)
            {
                auto& w           = writes[i];
                w                 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                w.dstSet          = modelSet;
                w.dstBinding      = i ? 16 : 15;
                w.descriptorCount = 1;
                w.descriptorType  = i ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.pBufferInfo     = &buffers[i];
            }
            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }
        void CreateRasterAADevice() override
        {
            AAExternalScene external;
            external.instance    = reinterpret_cast<void*>(instance);
            external.physical    = reinterpret_cast<void*>(physical);
            external.device      = reinterpret_cast<void*>(device);
            external.queueFamily = queueFamily;
            external.bindGBuffer = [this](void* command, const std::array<void*, 10>& views, int program) {
                auto activeSet = program == -5 ? gBufferSet : program == -6 ? oitColorSet : hybridGBufferSet;
                VkDescriptorImageInfo images[10]{};
                VkWriteDescriptorSet  writes[10]{};
                for (uint32_t i = 0; i < views.size(); i++)
                {
                    images[i].imageView   = reinterpret_cast<VkImageView>(views[i]);
                    images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    auto& w               = writes[i];
                    w                     = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    w.dstSet              = activeSet;
                    w.dstBinding          = i < 8 ? 17 + i : 31 + i - 8;
                    w.descriptorCount     = 1;
                    w.descriptorType      = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    w.pImageInfo          = &images[i];
                }
                // The reflection pass shares the shadow pass inputs.
                if (program != -8)
                    vkUpdateDescriptorSets(device, uint32_t(views.size()), writes, 0, nullptr);
                vkCmdBindDescriptorSets(
                  reinterpret_cast<VkCommandBuffer>(command),
                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                  layout,
                  1,
                  1,
                  &activeSet,
                  0,
                  nullptr);
            };
            external.beforeScene = [this](void* command) {
                // Update the shared descriptors before any pass binds the model set.
                if (!aaTriangle)
                    PrepareRasterAAInputs();
                EncodeProbeUpdate(reinterpret_cast<VkCommandBuffer>(command));
            };
            external.draw = [this](const AAPass& pass, void* command, void* targetPass) {
                auto      cmd        = reinterpret_cast<VkCommandBuffer>(command);
                auto      renderPass = reinterpret_cast<VkRenderPass>(targetPass);
                const int sample     = pass.multisampled ? 1 : 0;
                if (aaTriangle)
                {
                    if (!aaTrianglePipeline[sample])
                        aaTrianglePipeline[sample] = AAPipeline("triangle", false, sample ? 4 : 1, renderPass);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, aaTrianglePipeline[sample]);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    return;
                }
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &modelSet, 0, nullptr);
                if (pass.program == -5 || pass.program == -7 || pass.program == -8)
                {
                    const bool probeReflection = pass.program == -8 && probeFrame && environment
                                                 && hybrid.probeSkyLighting && hybrid.probeReflectionLighting;
                    const bool probes = (pass.program == -5 && probeFrame) || probeReflection;
                    const bool half = pass.program == -5 && pass.sources[9] != AADummy;
                    auto&      active = probeReflection      ? aaProbeReflection
                                        : pass.program == -8 ? aaHybridReflection
                                        : pass.program == -7 ? aaHybridShadow
                                        : probes && half     ? probeDeferredHalf
                                        : probes             ? probeDeferredPipeline
                                        : half               ? aaDeferredHalf
                                                             : aaDeferred;
                    if (probes)
                        vkCmdBindDescriptorSets(
                          cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2, 1, &probeReadSet, 0, nullptr);
                    if (!active)
                        active = AAPipeline(
                          probeReflection      ? "model-probe-reflection"
                          : pass.program == -8 ? "model-hybrid-reflection"
                          : pass.program == -7 ? "model-hybrid-shadow"
                          : probes && half     ? "model-probe-deferred-half"
                          : probes             ? "model-probe-deferred"
                          : half               ? "model-deferred-half"
                                               : "model-deferred",
                          true,
                          1,
                          renderPass);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    return;
                }
                const bool oit     = pass.program == -6;
                const bool gbuffer = pass.program == -4;
                const bool motion  = pass.program == -2;
                if (!gbuffer && !oit && environment && environmentSettings.showBackground)
                {
                    auto& pipeline = motion ? aaSkyMotion : aaSky[sample];
                    if (!pipeline)
                        pipeline = AAPipeline(motion ? "sky-motion" : "skybox", true, sample ? 4 : 1, renderPass);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                }
                if (!model->vertices.empty())
                {
                    auto& pipeline = oit ? aaOit : gbuffer ? aaGBuffer : motion ? aaMotion : aaModel[sample];
                    if (!pipeline)
                        pipeline = AAPipeline(
                          oit       ? "model-oit"
                          : gbuffer ? "model-gbuffer"
                          : motion  ? "model-motion"
                                    : "model",
                          false,
                          sample ? 4 : 1,
                          renderPass);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    if (scene.active)
                    {
                        for (const auto& draw : scene.draws)
                            if (draw.instanceCount)
                                vkCmdDraw(
                                  cmd, draw.vertexCount, draw.instanceCount, draw.firstVertex, draw.firstInstance);
                    }
                    else
                        vkCmdDraw(cmd, uint32_t(model->vertices.size()), 1, 0, 0);
                }
            };
            rasterAA.device = CreateVulkanAA(config, external);
        }
        void DestroyRasterAA()
        {
            rasterAA.Clear();
            auto release = [&](VkPipeline& pipeline) {
                if (pipeline)
                    vkDestroyPipeline(device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            };
            for (auto& pipeline : aaModel)
                release(pipeline);
            for (auto& pipeline : aaSky)
                release(pipeline);
            for (auto& pipeline : aaTrianglePipeline)
                release(pipeline);
            release(aaOit);
            release(aaGBuffer);
            release(aaDeferred);
            release(probeDeferredPipeline);
            release(aaDeferredHalf);
            release(probeDeferredHalf);
            release(aaHybridShadow);
            release(aaHybridReflection);
            release(aaProbeReflection);
            release(aaMotion);
            release(aaSkyMotion);
            destroyModelBuffer(aaPreviousUniform);
            destroyModelBuffer(aaPreviousPalette);
            aaPreviousPaletteBytes = 0;
        }


        void initInstance()
        {
            check(volkInitialize(), "Load Vulkan (install a Vulkan driver or SDK/MoltenVK)");
            uint32_t count = 0;
            check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "Count instance extensions");
            std::vector<VkExtensionProperties> available(count);
            check(vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()), "Instance extensions");
            const char* const* required = VulkanSurfaceExtensions(window, count);
            if (!required)
                throw std::runtime_error("Window host found no Vulkan surface support");
            std::vector<const char*> extensions(required, required + count);
            VkInstanceCreateInfo     info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            if (hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
            {
                if (std::none_of(extensions.begin(), extensions.end(), [](const char* extension) {
                        return std::strcmp(extension, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0;
                    }))
                    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            }
            if (hasExtension(available, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
                extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
            std::vector<const char*> layers;
            if (config.enableValidation)
            {
                check(vkEnumerateInstanceLayerProperties(&count, nullptr), "Count layers");
                std::vector<VkLayerProperties> availableLayers(count);
                check(vkEnumerateInstanceLayerProperties(&count, availableLayers.data()), "Layers");
                for (const auto& layer : availableLayers)
                    if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                        layers.push_back("VK_LAYER_KHRONOS_validation");
                if (layers.empty())
                    std::cerr << "Validation layer unavailable; continuing without it.\n";
            }
            const bool debug = !layers.empty() && hasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            if (debug)
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            VkDebugUtilsMessengerCreateInfoEXT debugInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            debugInfo.messageSeverity =
              VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                                    | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                    | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugInfo.pfnUserCallback = debugMessage;
            debugInfo.pUserData       = &validationErrors;
            VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            app.pApplicationName         = "Nova Render";
            app.apiVersion               = VK_API_VERSION_1_0;
            info.pApplicationInfo        = &app;
            info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
            info.ppEnabledExtensionNames = extensions.data();
            info.enabledLayerCount       = static_cast<uint32_t>(layers.size());
            info.ppEnabledLayerNames     = layers.data();
            if (debug)
                info.pNext = &debugInfo;
            check(vkCreateInstance(&info, nullptr, &instance), "Create instance");
            volkLoadInstance(instance);
            if (debug)
                check(
                  vkCreateDebugUtilsMessengerEXT(instance, &debugInfo, nullptr, &messenger), "Create debug messenger");
            std::cout << "Validation: " << (debug ? "enabled" : "disabled") << '\n';
        }
        void initDevice()
        {
            uint32_t count = 0;
            check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "Count GPUs");
            std::vector<VkPhysicalDevice> devices(count);
            check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "Enumerate GPUs");
            std::vector<VkExtensionProperties> extensions;
            for (auto candidate : devices)
            {
                check(
                  vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr), "Count device extensions");
                extensions.resize(count);
                check(
                  vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, extensions.data()),
                  "Device extensions");
                if (!hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                    continue;
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
                std::vector<VkQueueFamilyProperties> families(count);
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
                for (uint32_t i = 0; i < count; ++i)
                {
                    VkBool32 present = false;
                    check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "Surface support");
                    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
                    {
                        physical    = candidate;
                        queueFamily = i;
                        break;
                    }
                }
                if (physical)
                    break;
            }
            if (!physical)
                throw std::runtime_error("No GPU with a graphics/present queue and swapchain support");
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physical, &properties);
            std::cout << "GPU: " << properties.deviceName << '\n';
            std::vector<const char*> enabled{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            if (hasExtension(extensions, "VK_KHR_portability_subset"))
                enabled.push_back("VK_KHR_portability_subset");
            float                   priority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex = queueFamily;
            queueInfo.queueCount       = 1;
            queueInfo.pQueuePriorities = &priority;
            VkPhysicalDeviceFeatures supported{}, features{};
            vkGetPhysicalDeviceFeatures(physical, &supported);
            features.samplerAnisotropy = supported.samplerAnisotropy;
            if (!supported.independentBlend)
                throw std::runtime_error("Vulkan requires independentBlend for material transparency");
            features.independentBlend = VK_TRUE;
            VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            info.pEnabledFeatures        = &features;
            info.queueCreateInfoCount    = 1;
            info.pQueueCreateInfos       = &queueInfo;
            info.enabledExtensionCount   = static_cast<uint32_t>(enabled.size());
            info.ppEnabledExtensionNames = enabled.data();
            check(vkCreateDevice(physical, &info, nullptr, &device), "Create device");
            volkLoadDevice(device);
            vkGetDeviceQueue(device, queueFamily, 0, &queue);
        }
        void initCommands()
        {
            VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            info.queueFamilyIndex = queueFamily;
            info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            check(vkCreateCommandPool(device, &info, nullptr, &pool), "Create command pool");
            VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc.commandPool        = pool;
            alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device, &alloc, &cmd), "Allocate commands");
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            check(vkCreateSemaphore(device, &semaphore, nullptr, &acquired), "Create acquire semaphore");
            VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "Create fence");
        }
        void createSwapchain()
        {
            VkSurfaceCapabilitiesKHR caps{};
            check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "Surface capabilities");
            uint32_t count = 0;
            check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr), "Count surface formats");
            if (!count)
                throw std::runtime_error("Surface has no formats");
            std::vector<VkSurfaceFormatKHR> formats(count);
            check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()), "Surface formats");
            auto chosen = formats.front();
            for (auto f : formats)
                if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                    chosen = f;
            if (chosen.format == VK_FORMAT_UNDEFINED)
                chosen.format = VK_FORMAT_B8G8R8A8_SRGB;
            format = chosen.format;
            extent = caps.currentExtent;
            if (extent.width == std::numeric_limits<uint32_t>::max())
            {
                extent.width =
                  std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
                extent.height =
                  std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
            }
            count = caps.minImageCount + 1;
            if (caps.maxImageCount && count > caps.maxImageCount)
                count = caps.maxImageCount;
            VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
            info.surface          = surface;
            info.minImageCount    = count;
            info.imageFormat      = format;
            info.imageColorSpace  = chosen.colorSpace;
            info.imageExtent      = extent;
            info.imageArrayLayers = 1;
            info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            info.preTransform     = caps.currentTransform;
#ifdef __ANDROID__
            // Keep Nova viewports in the SDL window's orientation and let Android compose the rotation.
            if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
#endif
            for (auto alpha :
                 {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                  VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                  VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                  VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
                if (caps.supportedCompositeAlpha & alpha)
                {
                    info.compositeAlpha = alpha;
                    break;
                }
            info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            info.clipped     = VK_TRUE;
            check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "Create swapchain");
            check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "Count swapchain images");
            std::vector<VkImage> images(count);
            check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()), "Swapchain images");
            views.resize(count, VK_NULL_HANDLE);
            finished.resize(count, VK_NULL_HANDLE);
            for (uint32_t i = 0; i < count; ++i)
            {
                VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                view.image            = images[i];
                view.viewType         = VK_IMAGE_VIEW_TYPE_2D;
                view.format           = format;
                view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                check(vkCreateImageView(device, &view, nullptr, &views[i]), "Create image view");
                VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                check(vkCreateSemaphore(device, &sem, nullptr, &finished[i]), "Create present semaphore");
            }
            if (model)
                modelImage(
                  modelDepth,
                  extent.width,
                  extent.height,
                  VK_FORMAT_D32_SFLOAT,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT);
            createPipeline();
            framebuffers.resize(count, VK_NULL_HANDLE);
            for (uint32_t i = 0; i < count; ++i)
            {
                VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                VkImageView             attachments[] = {views[i], modelDepth.view};
                fb.renderPass                         = renderPass;
                fb.attachmentCount                    = model ? 2 : 1;
                fb.pAttachments                       = attachments;
                fb.width                              = extent.width;
                fb.height                             = extent.height;
                fb.layers                             = 1;
                check(vkCreateFramebuffer(device, &fb, nullptr, &framebuffers[i]), "Create framebuffer");
            }
            resized = false;
            std::cout << "Swapchain: " << extent.width << 'x' << extent.height << '\n';
        }
        VkShaderModule shader(const char* name)
        {
            const auto data  = LoadShader(config.shaderDirectory, name);
            const auto bytes = data.size();
            if (bytes % 4 != 0)
                throw std::runtime_error("Invalid SPIR-V size");
            std::vector<uint32_t> code(bytes / 4);
            std::memcpy(code.data(), data.data(), bytes);
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize         = static_cast<size_t>(bytes);
            info.pCode            = code.data();
            VkShaderModule module = VK_NULL_HANDLE;
            check(vkCreateShaderModule(device, &info, nullptr, &module), "Create shader module");
            return module;
        }
        void createPipeline()
        {
            VkAttachmentDescription attachment{};
            attachment.format         = format;
            attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            VkAttachmentDescription attachments[2]{attachment, {}};
            attachments[1].format         = VK_FORMAT_D32_SFLOAT;
            attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
            attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkSubpassDescription  subpass{};
            subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments    = &color;
            if (model)
                subpass.pDepthStencilAttachment = &depthRef;
            VkSubpassDependency dep{};
            dep.srcSubpass   = VK_SUBPASS_EXTERNAL;
            dep.dstSubpass   = 0;
            dep.srcStageMask = dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep.dstAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            if (model)
            {
                dep.srcStageMask |=
                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                dep.dstStageMask  = dep.srcStageMask;
                dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                dep.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }
            VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            rp.attachmentCount = model ? 2 : 1;
            rp.pAttachments    = attachments;
            rp.subpassCount    = 1;
            rp.pSubpasses      = &subpass;
            rp.dependencyCount = 1;
            rp.pDependencies   = &dep;
            check(vkCreateRenderPass(device, &rp, nullptr, &renderPass), "Create render pass");
            VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            VkDescriptorSetLayout      modelLayouts[] = {modelSetLayout, gBufferSetLayout, probeSetLayout};
            if (model)
            {
                pl.setLayoutCount = 3;
                pl.pSetLayouts    = modelLayouts;
            }
            check(vkCreatePipelineLayout(device, &pl, nullptr, &layout), "Create pipeline layout");
            VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
            try
            {
                vert = shader(model ? "model.vert.spv" : "triangle.vert.spv");
                frag = shader(model ? "model.frag.spv" : "triangle.frag.spv");
                VkPipelineShaderStageCreateInfo stages[2]{};
                for (auto& stage : stages)
                {
                    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                }
                stages[0].pName  = "VSMain";
                stages[1].pName  = "PSMain";
                stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = vert;
                stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = frag;
                VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
                VkPipelineInputAssemblyStateCreateInfo assembly{
                  VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
                assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
                viewport.viewportCount = viewport.scissorCount = 1;
                VkPipelineRasterizationStateCreateInfo raster{
                  VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
                raster.polygonMode = VK_POLYGON_MODE_FILL;
                raster.cullMode    = VK_CULL_MODE_NONE;
                raster.lineWidth   = 1.0f;
                VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
                ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                VkPipelineColorBlendAttachmentState blend{};
                blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                                       | VK_COLOR_COMPONENT_A_BIT;
                VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
                blending.attachmentCount = 1;
                blending.pAttachments    = &blend;
                VkDynamicState                   states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
                VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
                dynamic.dynamicStateCount = 2;
                dynamic.pDynamicStates    = states;
                VkPipelineDepthStencilStateCreateInfo depthState{
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
                depthState.depthTestEnable = depthState.depthWriteEnable = VK_TRUE;
                depthState.depthCompareOp                                = VK_COMPARE_OP_LESS;
                VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                info.stageCount          = 2;
                info.pStages             = stages;
                info.pVertexInputState   = &vertex;
                info.pInputAssemblyState = &assembly;
                info.pViewportState      = &viewport;
                info.pRasterizationState = &raster;
                info.pMultisampleState   = &ms;
                if (model)
                    info.pDepthStencilState = &depthState;
                info.pColorBlendState = &blending;
                info.pDynamicState    = &dynamic;
                info.layout           = layout;
                info.renderPass       = renderPass;
                check(
                  vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "Create pipeline");
                if (model)
                {
                    VkShaderModule skyVert = VK_NULL_HANDLE, skyFrag = VK_NULL_HANDLE;
                    try
                    {
                        skyVert                     = shader("skybox.vert.spv");
                        skyFrag                     = shader("skybox.frag.spv");
                        stages[0].module            = skyVert;
                        stages[0].pName             = "SkyVSMain";
                        stages[1].module            = skyFrag;
                        stages[1].pName             = "SkyPSMain";
                        depthState.depthTestEnable  = VK_FALSE;
                        depthState.depthWriteEnable = VK_FALSE;
                        info.pDepthStencilState     = &depthState;
                        check(
                          vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &skyPipeline),
                          "Create skybox pipeline");
                    }
                    catch (...)
                    {
                        if (skyVert)
                            vkDestroyShaderModule(device, skyVert, nullptr);
                        if (skyFrag)
                            vkDestroyShaderModule(device, skyFrag, nullptr);
                        throw;
                    }
                    vkDestroyShaderModule(device, skyVert, nullptr);
                    vkDestroyShaderModule(device, skyFrag, nullptr);
                }
            }
            catch (...)
            {
                if (vert)
                    vkDestroyShaderModule(device, vert, nullptr);
                if (frag)
                    vkDestroyShaderModule(device, frag, nullptr);
                throw;
            }
            vkDestroyShaderModule(device, vert, nullptr);
            vkDestroyShaderModule(device, frag, nullptr);
        }
        FrameResult draw()
        {
            if (!width || !height)
                return FrameResult::Skipped;
#ifdef __ANDROID__
            const auto nativeWindow = window.NativeHandle();
            if (!nativeWindow)
                return FrameResult::Skipped;
            if (surfaceLost || nativeWindow != surfaceWindow)
            {
                WaitIdle();
                destroySwapchain();
                DestroyVulkanSurface(window, instance, surface);
                surface = VK_NULL_HANDLE;
                surface = CreateVulkanSurface(window, instance);
                surfaceWindow = nativeWindow;
                surfaceLost = false;
            }
#endif
            if (model && !modelReady)
            {
                try
                {
                    uploadModel();
                }
                catch (...)
                {
                    WaitIdle();
                    destroyModel();
                    throw;
                }
            }
            if (resized || !swapchain)
            {
                check(vkDeviceWaitIdle(device), "Wait before resize");
                destroySwapchain();
                createSwapchain();
            }
            check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "Wait frame");
#ifdef NOVA_HAS_UI
            if (panelEnabled)
            {
                parameterPanel->Activate();
                if (!uiRenderer)
                {
                    ImGui_ImplVulkan_InitInfo info{};
                    info.ApiVersion         = VK_API_VERSION_1_0;
                    info.Instance           = instance;
                    info.PhysicalDevice     = physical;
                    info.Device             = device;
                    info.QueueFamily        = queueFamily;
                    info.Queue              = queue;
                    info.RenderPass         = renderPass;
                    info.MinImageCount      = 2;
                    info.ImageCount         = static_cast<uint32_t>(views.size());
                    info.MSAASamples        = VK_SAMPLE_COUNT_1_BIT;
                    info.DescriptorPoolSize = 32;
                    info.CheckVkResultFn    = [](VkResult result) { check(result, "ImGui Vulkan"); };
                    if (!ImGui_ImplVulkan_Init(&info))
                        throw std::runtime_error("Initialize Vulkan UI failed");
                    uiRenderer = true;
                }
                parameterPanel->SetSRGBOutput(format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB);
                ImGui_ImplVulkan_NewFrame();
                DrawParameterPanel();
            }
#endif
            if (model && !model->skinInputs.empty())
            {
                void* mapped;
                check(
                  vkMapMemory(
                    device, modelJoints.memory, 0, model->jointMatrices.size() * sizeof(JointMatrix), 0, &mapped),
                  "Map joint matrices");
                std::memcpy(mapped, model->jointMatrices.data(), model->jointMatrices.size() * sizeof(JointMatrix));
                vkUnmapMemory(device, modelJoints.memory);
            }
            if (model)
            {
                size_t bytes = std::max(size_t(192), RasterInstances().size() * sizeof(InstanceData));
                if (bytes > sceneInstanceBytes)
                {
                    destroyModelBuffer(sceneInstanceBuffer);
                    modelBuffer(sceneInstanceBuffer, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
                    sceneInstanceBytes = bytes;
                    VkDescriptorBufferInfo info{sceneInstanceBuffer.buffer, 0, VK_WHOLE_SIZE};
                    VkWriteDescriptorSet   write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    write.dstSet          = modelSet;
                    write.dstBinding      = 14;
                    write.descriptorCount = 1;
                    write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    write.pBufferInfo     = &info;
                    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
                }
                if (!RasterInstances().empty())
                {
                    void* mapped = nullptr;
                    check(
                      vkMapMemory(
                        device,
                        sceneInstanceBuffer.memory,
                        0,
                        RasterInstances().size() * sizeof(InstanceData),
                        0,
                        &mapped),
                      "Map instances");
                    std::memcpy(mapped, RasterInstances().data(), RasterInstances().size() * sizeof(InstanceData));
                    vkUnmapMemory(device, sceneInstanceBuffer.memory);
                }
            }
            const auto rectangle  = SceneViewport(extent.width, extent.height);
            const auto sceneWidth = rectangle.width, sceneHeight = rectangle.height;
            if (!sceneWidth || !sceneHeight)
                return FrameResult::Skipped;
            const auto* path     = DrawPathFrame(sceneWidth, sceneHeight);
            const bool  gpuFrame = path && path->pixels.empty() && rasterAA.lastGPUFrame;
            if (gpuFrame)
                BindRasterGPUOutput();
            else if (path)
                UploadPathImage(*path);
            if (model)
            {
                auto uniform = MakeSceneUniforms(
                  *model,
                  GetViewCamera(),
                  float(sceneWidth) / sceneHeight,
                  lighting,
                  directionalLight,
                  environment.get(),
                  environmentSettings);
                uniform.skyParams[2]      = gpuFrame ? 2.f : path ? 1.0f : 0.0f;
                uniform.lightDirection[3] = MaterialMode();
                ApplyRasterSettings(uniform);
                void* mapped = nullptr;
                check(vkMapMemory(device, modelUniform.memory, 0, sizeof(uniform), 0, &mapped), "Map scene uniforms");
                std::memcpy(mapped, &uniform, sizeof(uniform));
                vkUnmapMemory(device, modelUniform.memory);
            }
            uint32_t image  = 0;
            auto     result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &image);
#ifdef __ANDROID__
            if (result == VK_ERROR_SURFACE_LOST_KHR)
            {
                surfaceLost = true;
                return FrameResult::Skipped;
            }
#endif
            if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                resized = true;
                return FrameResult::Skipped;
            }
            if (result != VK_SUBOPTIMAL_KHR)
                check(result, "Acquire image");
            const bool suboptimal = result == VK_SUBOPTIMAL_KHR;
            check(vkResetCommandBuffer(cmd, 0), "Reset commands");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            check(vkBeginCommandBuffer(cmd, &begin), "Begin commands");
            VkClearValue clear{};
            const auto   background =
              ClearColorForTarget(format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB);
            std::copy(background.begin(), background.end(), clear.color.float32);
            VkClearValue clearValues[2]{clear, {}};
            clearValues[1].depthStencil = {1.0f, 0};
            VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            pass.renderPass        = renderPass;
            pass.framebuffer       = framebuffers[image];
            pass.renderArea.extent = extent;
            pass.clearValueCount   = model ? 2 : 1;
            pass.pClearValues      = clearValues;
            vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            VkViewport viewport{float(rectangle.x), float(rectangle.y), float(sceneWidth), float(sceneHeight), 0, 1};
            VkRect2D   scissor{{int32_t(rectangle.x), int32_t(rectangle.y)}, {sceneWidth, sceneHeight}};
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            if (model)
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &modelSet, 0, nullptr);
            if ((environment && environmentSettings.showBackground) || path)
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
                vkCmdDraw(cmd, 3, 1, 0, 0);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            }
            if (!path && scene.active)
            {
                for (const auto& draw : scene.draws)
                    if (draw.instanceCount)
                        vkCmdDraw(cmd, draw.vertexCount, draw.instanceCount, draw.firstVertex, draw.firstInstance);
            }
            else if (!path && (!model || !model->vertices.empty()))
                vkCmdDraw(cmd, model ? static_cast<uint32_t>(model->vertices.size()) : 3, 1, 0, 0);
#ifdef NOVA_HAS_UI
            if (panelEnabled)
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
#endif
            vkCmdEndRenderPass(cmd);
            check(vkEndCommandBuffer(cmd), "End commands");
            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo         submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.waitSemaphoreCount   = 1;
            submit.pWaitSemaphores      = &acquired;
            submit.pWaitDstStageMask    = &waitStage;
            submit.commandBufferCount   = 1;
            submit.pCommandBuffers      = &cmd;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores    = &finished[image];
            check(vkResetFences(device, 1, &fence), "Reset fence");
            check(vkQueueSubmit(queue, 1, &submit, fence), "Submit");
            VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores    = &finished[image];
            present.swapchainCount     = 1;
            present.pSwapchains        = &swapchain;
            present.pImageIndices      = &image;
            result                     = vkQueuePresentKHR(queue, &present);
#ifdef __ANDROID__
            if (result == VK_ERROR_SURFACE_LOST_KHR)
            {
                surfaceLost = true;
                return FrameResult::Skipped;
            }
#endif
            if (result != VK_SUCCESS && result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
                check(result, "Present");
            resized = suboptimal || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR;
            return result == VK_ERROR_OUT_OF_DATE_KHR ? FrameResult::Skipped : FrameResult::Rendered;
        }
        void destroySwapchain()
        {
#ifdef NOVA_HAS_UI
            if (uiRenderer)
            {
                parameterPanel->Activate();
                ImGui_ImplVulkan_Shutdown();
                uiRenderer = false;
            }
#endif
            for (auto fb : framebuffers)
                if (fb)
                    vkDestroyFramebuffer(device, fb, nullptr);
            framebuffers.clear();
            destroyModelTexture(modelDepth);
            if (skyPipeline)
                vkDestroyPipeline(device, skyPipeline, nullptr);
            skyPipeline = VK_NULL_HANDLE;
            if (pipeline)
                vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
            if (layout)
                vkDestroyPipelineLayout(device, layout, nullptr);
            layout = VK_NULL_HANDLE;
            if (renderPass)
                vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
            for (auto view : views)
                if (view)
                    vkDestroyImageView(device, view, nullptr);
            views.clear();
            for (auto sem : finished)
                if (sem)
                    vkDestroySemaphore(device, sem, nullptr);
            finished.clear();
            if (swapchain)
                vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
        void cleanup() noexcept
        {
            if (device)
            {
                vkDeviceWaitIdle(device);
                DestroyRasterAA();
                destroySwapchain();
                destroyModel();
                if (fence)
                    vkDestroyFence(device, fence, nullptr);
                if (acquired)
                    vkDestroySemaphore(device, acquired, nullptr);
                if (pool)
                    vkDestroyCommandPool(device, pool, nullptr);
                vkDestroyDevice(device, nullptr);
                device = VK_NULL_HANDLE;
            }
            if (surface)
            {
                DestroyVulkanSurface(window, instance, surface);
                surface = VK_NULL_HANDLE;
            }
            if (messenger)
            {
                vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
                messenger = VK_NULL_HANDLE;
            }
            if (instance)
            {
                vkDestroyInstance(instance, nullptr);
                instance = VK_NULL_HANDLE;
            }
        }
    };

    std::unique_ptr<Backend> CreateVulkanBackend(PresentationWindow window, const RendererConfig& config)
    {
        auto backend = std::make_unique<VulkanBackend>();
        backend->initialize(window, config);
        return backend;
    }
} // namespace render::detail
