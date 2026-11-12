#include "../Backend.hpp"
#include <volk.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

static void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
}
static bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& e) {
        return std::strcmp(e.extensionName, name) == 0;
    });
}
static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ++*static_cast<unsigned*>(user);
    std::cerr << "[Vulkan] " << data->pMessage << '\n';
    return VK_FALSE;
}

namespace render::detail {
struct VulkanBackend final : Backend {
    ~VulkanBackend() { cleanup(); }
    void initialize(GLFWwindow* target, const RendererConfig& settings) {
        if (!target) throw std::invalid_argument("Renderer requires a GLFW window");
        window = target;
        config = settings;
        initInstance();
        check(glfwCreateWindowSurface(instance, window, nullptr, &surface), "Create surface");
        initDevice();
        initCommands();
    }
    void Resize(uint32_t newWidth, uint32_t newHeight) override {
        if (width == newWidth && height == newHeight) return;
        width = newWidth; height = newHeight; resized = true;
    }
    FrameResult DrawFrame() override { return draw(); }
    void WaitIdle() override { check(vkDeviceWaitIdle(device), "Wait idle"); }
    uint32_t ValidationErrorCount() const override { return validationErrors; }
    GLFWwindow* window = nullptr;
    RendererConfig config;
    uint32_t width = 0, height = 0;
    bool resized = true;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    unsigned validationErrors = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> finished;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    void initInstance() {
        check(volkInitialize(), "Load Vulkan (install a Vulkan driver or SDK/MoltenVK)");
        uint32_t count = 0;
        check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "Count instance extensions");
        std::vector<VkExtensionProperties> available(count);
        check(vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()), "Instance extensions");
        const char** required = glfwGetRequiredInstanceExtensions(&count);
        if (!required) throw std::runtime_error("GLFW found no Vulkan surface support");
        std::vector<const char*> extensions(required, required + count);
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        if (hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
        if (hasExtension(available, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        std::vector<const char*> layers;
        if (config.enableValidation) {
            check(vkEnumerateInstanceLayerProperties(&count, nullptr), "Count layers");
            std::vector<VkLayerProperties> availableLayers(count);
            check(vkEnumerateInstanceLayerProperties(&count, availableLayers.data()), "Layers");
            for (const auto& layer : availableLayers)
                if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                    layers.push_back("VK_LAYER_KHRONOS_validation");
            if (layers.empty()) std::cerr << "Validation layer unavailable; continuing without it.\n";
        }
        const bool debug = !layers.empty() && hasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (debug) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = debugMessage;
        debugInfo.pUserData = &validationErrors;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Nova Triangle";
        app.apiVersion = VK_API_VERSION_1_0;
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        info.enabledLayerCount = static_cast<uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.data();
        if (debug) info.pNext = &debugInfo;
        check(vkCreateInstance(&info, nullptr, &instance), "Create instance");
        volkLoadInstance(instance);
        if (debug) check(vkCreateDebugUtilsMessengerEXT(instance, &debugInfo, nullptr, &messenger), "Create debug messenger");
        std::cout << "Validation: " << (debug ? "enabled" : "disabled") << '\n';
    }
    void initDevice() {
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "Count GPUs");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "Enumerate GPUs");
        std::vector<VkExtensionProperties> extensions;
        for (auto candidate : devices) {
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr), "Count device extensions");
            extensions.resize(count);
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, extensions.data()), "Device extensions");
            if (!hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
            for (uint32_t i = 0; i < count; ++i) {
                VkBool32 present = false;
                check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "Surface support");
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    physical = candidate; queueFamily = i; break;
                }
            }
            if (physical) break;
        }
        if (!physical) throw std::runtime_error("No GPU with a graphics/present queue and swapchain support");
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        std::cout << "GPU: " << properties.deviceName << '\n';
        std::vector<const char*> enabled{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (hasExtension(extensions, "VK_KHR_portability_subset")) enabled.push_back("VK_KHR_portability_subset");
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = queueFamily; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &queueInfo;
        info.enabledExtensionCount = static_cast<uint32_t>(enabled.size()); info.ppEnabledExtensionNames = enabled.data();
        check(vkCreateDevice(physical, &info, nullptr, &device), "Create device");
        volkLoadDevice(device);
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
    }
    void initCommands() {
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.queueFamilyIndex = queueFamily; info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device, &info, nullptr, &pool), "Create command pool");
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = pool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &alloc, &cmd), "Allocate commands");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &semaphore, nullptr, &acquired), "Create acquire semaphore");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "Create fence");
    }
    void createSwapchain() {
        VkSurfaceCapabilitiesKHR caps{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "Surface capabilities");
        uint32_t count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr), "Count surface formats");
        if (!count) throw std::runtime_error("Surface has no formats");
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()), "Surface formats");
        auto chosen = formats.front();
        for (auto f : formats) if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) chosen = f;
        if (chosen.format == VK_FORMAT_UNDEFINED) chosen.format = VK_FORMAT_B8G8R8A8_SRGB;
        format = chosen.format;
        extent = caps.currentExtent;
        if (extent.width == std::numeric_limits<uint32_t>::max()) {
            extent.width = std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        count = caps.minImageCount + 1;
        if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface; info.minImageCount = count; info.imageFormat = format;
        info.imageColorSpace = chosen.colorSpace; info.imageExtent = extent; info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        for (auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & alpha) { info.compositeAlpha = alpha; break; }
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR; info.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "Create swapchain");
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "Count swapchain images");
        std::vector<VkImage> images(count);
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()), "Swapchain images");
        views.resize(count, VK_NULL_HANDLE); finished.resize(count, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < count; ++i) {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device, &view, nullptr, &views[i]), "Create image view");
            VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            check(vkCreateSemaphore(device, &sem, nullptr, &finished[i]), "Create present semaphore");
        }
        createPipeline();
        framebuffers.resize(count, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < count; ++i) {
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass = renderPass; fb.attachmentCount = 1; fb.pAttachments = &views[i];
            fb.width = extent.width; fb.height = extent.height; fb.layers = 1;
            check(vkCreateFramebuffer(device, &fb, nullptr, &framebuffers[i]), "Create framebuffer");
        }
        resized = false;
        std::cout << "Swapchain: " << extent.width << 'x' << extent.height << '\n';
    }
    VkShaderModule shader(const char* name) {
        auto path = (config.shaderDirectory / name).string();
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Cannot open shader: " + path);
        auto bytes = file.tellg();
        if (bytes <= 0 || bytes % 4 != 0) throw std::runtime_error("Invalid SPIR-V: " + path);
        std::vector<uint32_t> code(static_cast<size_t>(bytes) / 4);
        file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), bytes);
        if (!file) throw std::runtime_error("Cannot read shader: " + path);
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = static_cast<size_t>(bytes); info.pCode = code.data();
        VkShaderModule module = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device, &info, nullptr, &module), "Create shader module");
        return module;
    }
    void createPipeline() {
        VkAttachmentDescription attachment{};
        attachment.format = format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
        VkSubpassDependency dep{}; dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
        dep.srcStageMask = dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1; rp.pAttachments = &attachment; rp.subpassCount = 1; rp.pSubpasses = &subpass;
        rp.dependencyCount = 1; rp.pDependencies = &dep;
        check(vkCreateRenderPass(device, &rp, nullptr, &renderPass), "Create render pass");
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        check(vkCreatePipelineLayout(device, &pl, nullptr, &layout), "Create pipeline layout");
        VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
        try {
            vert = shader("triangle.vert.spv"); frag = shader("triangle.frag.spv");
            VkPipelineShaderStageCreateInfo stages[2]{};
            for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;  }
            stages[0].pName = "VSMain"; stages[1].pName = "PSMain";
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag;
            VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState blend{};
            blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blending.attachmentCount = 1; blending.pAttachments = &blend;
            VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = states;
            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &vertex; info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport; info.pRasterizationState = &raster; info.pMultisampleState = &ms;
            info.pColorBlendState = &blending; info.pDynamicState = &dynamic; info.layout = layout; info.renderPass = renderPass;
            check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "Create pipeline");
        } catch (...) {
            if (vert) vkDestroyShaderModule(device, vert, nullptr);
            if (frag) vkDestroyShaderModule(device, frag, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, vert, nullptr); vkDestroyShaderModule(device, frag, nullptr);
    }
    FrameResult draw() {
        if (!width || !height) return FrameResult::Skipped;
        if (resized || !swapchain) {
            check(vkDeviceWaitIdle(device), "Wait before resize");
            destroySwapchain();
            createSwapchain();
        }
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "Wait frame");
        uint32_t image = 0;
        auto result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &image);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { resized = true; return FrameResult::Skipped; }
        if (result != VK_SUBOPTIMAL_KHR) check(result, "Acquire image");
        const bool suboptimal = result == VK_SUBOPTIMAL_KHR;
        check(vkResetCommandBuffer(cmd, 0), "Reset commands");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check(vkBeginCommandBuffer(cmd, &begin), "Begin commands");
        VkClearValue clear{}; clear.color = {{0.025f, 0.035f, 0.055f, 1.0f}};
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass = renderPass; pass.framebuffer = framebuffers[image]; pass.renderArea.extent = extent;
        pass.clearValueCount = 1; pass.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport); vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        check(vkEndCommandBuffer(cmd), "End commands");
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &acquired; submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &finished[image];
        check(vkResetFences(device, 1, &fence), "Reset fence");
        check(vkQueueSubmit(queue, 1, &submit, fence), "Submit");
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &finished[image];
        present.swapchainCount = 1; present.pSwapchains = &swapchain; present.pImageIndices = &image;
        result = vkQueuePresentKHR(queue, &present);
        if (result != VK_SUCCESS && result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
            check(result, "Present");
        resized = suboptimal || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR;
        return result == VK_ERROR_OUT_OF_DATE_KHR ? FrameResult::Skipped : FrameResult::Rendered;
    }
    void destroySwapchain() {
        for (auto fb : framebuffers) if (fb) vkDestroyFramebuffer(device, fb, nullptr);
        framebuffers.clear();
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        layout = VK_NULL_HANDLE;
        if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
        for (auto view : views) if (view) vkDestroyImageView(device, view, nullptr);
        views.clear();
        for (auto sem : finished) if (sem) vkDestroySemaphore(device, sem, nullptr);
        finished.clear();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    void cleanup() noexcept {
        if (device) {
            vkDeviceWaitIdle(device);
            destroySwapchain();
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (acquired) vkDestroySemaphore(device, acquired, nullptr);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr); device = VK_NULL_HANDLE;
        }
        if (surface) { vkDestroySurfaceKHR(instance, surface, nullptr); surface = VK_NULL_HANDLE; }
        if (messenger) { vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr); messenger = VK_NULL_HANDLE; }
        if (instance) { vkDestroyInstance(instance, nullptr); instance = VK_NULL_HANDLE; }
    }
};

std::unique_ptr<Backend> CreateVulkanBackend(GLFWwindow* window, const RendererConfig& config) {
    auto backend = std::make_unique<VulkanBackend>();
    backend->initialize(window, config);
    return backend;
}
} // namespace render::detail
