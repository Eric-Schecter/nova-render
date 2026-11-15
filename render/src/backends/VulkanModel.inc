struct ModelBuffer
{
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};
struct ModelTexture
{
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
};
ModelBuffer                 modelVertices, modelUniform, modelStaging, modelSkin, modelJoints;
std::array<ModelTexture, 5> modelTextures;
ModelTexture                modelDepth, environmentCube, backgroundCube, pathDisplay;
uint32_t                    pathWidth = 0, pathHeight = 0;
VkSampler                   modelSampler   = VK_NULL_HANDLE;
VkDescriptorSetLayout       modelSetLayout = VK_NULL_HANDLE;
VkDescriptorPool            modelPool      = VK_NULL_HANDLE;
VkDescriptorSet             modelSet       = VK_NULL_HANDLE;
bool                        modelReady     = false;
uint32_t                    memoryType(uint32_t bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("No suitable Vulkan memory type");
}
void modelBuffer(ModelBuffer& b, VkDeviceSize bytes, VkBufferUsageFlags usage, const void* source = nullptr)
{
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size        = bytes;
    info.usage       = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device, &info, nullptr, &b.buffer), "Create model buffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, b.buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex =
      memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device, &alloc, nullptr, &b.memory), "Allocate model buffer");
    check(vkBindBufferMemory(device, b.buffer, b.memory, 0), "Bind model buffer");
    if (source)
    {
        void* mapped = nullptr;
        check(vkMapMemory(device, b.memory, 0, bytes, 0, &mapped), "Map model buffer");
        std::memcpy(mapped, source, static_cast<size_t>(bytes));
        vkUnmapMemory(device, b.memory);
    }
}
void destroyModelBuffer(ModelBuffer& b)
{
    if (b.buffer)
        vkDestroyBuffer(device, b.buffer, nullptr);
    if (b.memory)
        vkFreeMemory(device, b.memory, nullptr);
    b = {};
}
void destroyModelTexture(ModelTexture& t)
{
    if (t.view)
        vkDestroyImageView(device, t.view, nullptr);
    if (t.image)
        vkDestroyImage(device, t.image, nullptr);
    if (t.memory)
        vkFreeMemory(device, t.memory, nullptr);
    t = {};
}
void modelImage(
  ModelTexture&      t,
  uint32_t           w,
  uint32_t           h,
  VkFormat           fmt,
  VkImageUsageFlags  usage,
  VkImageAspectFlags aspect,
  uint32_t           levels = 1,
  uint32_t           layers = 1,
  bool               cube   = false)
{
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags       = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType   = VK_IMAGE_TYPE_2D;
    info.format      = fmt;
    info.extent      = {w, h, 1};
    info.mipLevels   = levels;
    info.arrayLayers = layers;
    info.samples     = VK_SAMPLE_COUNT_1_BIT;
    info.tiling      = VK_IMAGE_TILING_OPTIMAL;
    info.usage       = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(device, &info, nullptr, &t.image), "Create model image");
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, t.image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize  = req.size;
    alloc.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device, &alloc, nullptr, &t.memory), "Allocate model image");
    check(vkBindImageMemory(device, t.image, t.memory, 0), "Bind model image");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image            = t.image;
    view.viewType         = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    view.format           = fmt;
    view.subresourceRange = {aspect, 0, levels, 0, layers};
    check(vkCreateImageView(device, &view, nullptr, &t.view), "Create model image view");
}
void uploadEnvironmentCube(ModelTexture& cube, const std::vector<EnvironmentMip>& mips)
{
    std::vector<uint16_t>          pixels;
    std::vector<VkBufferImageCopy> regions;
    for (uint32_t level = 0; level < mips.size(); ++level)
    {
        const auto& mip = mips[level];
        for (uint32_t face = 0; face < 6; ++face)
        {
            VkBufferImageCopy copy{};
            copy.bufferOffset     = pixels.size() * 2;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, face, 1};
            copy.imageExtent      = {mip.size, mip.size, 1};
            regions.push_back(copy);
            const auto start = mip.pixels.begin() + size_t(face) * mip.size * mip.size * 4;
            pixels.insert(pixels.end(), start, start + size_t(mip.size) * mip.size * 4);
        }
    }
    modelBuffer(modelStaging, pixels.size() * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels.data());
    modelImage(
      cube,
      mips[0].size,
      mips[0].size,
      VK_FORMAT_R16G16B16A16_SFLOAT,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT,
      static_cast<uint32_t>(mips.size()),
      6,
      true);
    check(vkResetCommandBuffer(cmd, 0), "Reset environment upload");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(cmd, &begin), "Begin environment upload");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                                             = cube.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(mips.size()), 0, 6};
    barrier.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(
      cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkCmdCopyBufferToImage(
      cmd,
      modelStaging.buffer,
      cube.image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      static_cast<uint32_t>(regions.size()),
      regions.data());
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
      cmd,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &barrier);
    check(vkEndCommandBuffer(cmd), "End environment upload");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "Submit environment upload");
    check(vkQueueWaitIdle(queue), "Wait environment upload");
    destroyModelBuffer(modelStaging);
}
void uploadModel()
{
    WaitIdle();
    modelBuffer(
      modelVertices,
      std::max(sizeof(ModelVertex), model->vertices.size() * sizeof(ModelVertex)),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      model->vertices.empty() ? nullptr : model->vertices.data());
    modelBuffer(
      modelSkin,
      std::max(size_t(64), model->skinInputs.size() * sizeof(SkinInput)),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      model->skinInputs.empty() ? nullptr : model->skinInputs.data());
    modelBuffer(
      modelJoints,
      std::max(size_t(64), model->jointMatrices.size() * sizeof(JointMatrix)),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      model->jointMatrices.empty() ? nullptr : model->jointMatrices.data());
    modelBuffer(modelUniform, sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    for (size_t i = 0; i < 5; ++i)
    {
        const auto                     mips = MaterialMipmaps(model->images[i], i);
        const auto&                    src  = mips[0];
        auto&                          dst  = modelTextures[i];
        std::vector<unsigned char>     pixels;
        std::vector<VkBufferImageCopy> copies;
        for (uint32_t level = 0; level < mips.size(); level++)
        {
            VkBufferImageCopy c{};
            c.bufferOffset     = pixels.size();
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            c.imageExtent      = {mips[level].width, mips[level].height, 1};
            copies.push_back(c);
            pixels.insert(pixels.end(), mips[level].pixels.begin(), mips[level].pixels.end());
        }
        modelBuffer(modelStaging, pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels.data());
        modelImage(
          dst,
          src.width,
          src.height,
          (i == 0 || i == 4) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT,
          uint32_t(mips.size()));
        check(vkResetCommandBuffer(cmd, 0), "Reset upload command");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check(vkBeginCommandBuffer(cmd, &begin), "Begin upload");
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                                             = dst.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, uint32_t(mips.size()), 0, 1};
        barrier.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(
          cmd,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          0,
          0,
          nullptr,
          0,
          nullptr,
          1,
          &barrier);
        vkCmdCopyBufferToImage(
          cmd,
          modelStaging.buffer,
          dst.image,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          uint32_t(copies.size()),
          copies.data());
        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
          cmd,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0,
          0,
          nullptr,
          0,
          nullptr,
          1,
          &barrier);
        check(vkEndCommandBuffer(cmd), "End upload");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;
        check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "Submit texture upload");
        check(vkQueueWaitIdle(queue), "Wait texture upload");
        destroyModelBuffer(modelStaging);
    }
    const auto& env = environment ? *environment : FallbackEnvironment();
    uploadEnvironmentCube(environmentCube, env.mips);
    uploadEnvironmentCube(backgroundCube, BackgroundMips(env));
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode                    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod                                                     = 32;
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physical, &features);
    sampler.anisotropyEnable = features.samplerAnisotropy;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    sampler.maxAnisotropy = std::min(8.f, properties.limits.maxSamplerAnisotropy);
    check(vkCreateSampler(device, &sampler, nullptr, &modelSampler), "Create model sampler");
    std::array<VkDescriptorSetLayoutBinding, 15> bindings{};
    for (uint32_t i = 0; i < 13; ++i)
    {
        bindings[i].binding         = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType  = i == 0                ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                      : (i == 1 || i >= 11) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                      : i == 7              ? VK_DESCRIPTOR_TYPE_SAMPLER
                                                            : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[i].stageFlags      = i == 0                ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                                      : (i == 1 || i >= 11) ? VK_SHADER_STAGE_VERTEX_BIT
                                                            : VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[13] = {
      15, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[14] = {16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 15;
    layoutInfo.pBindings    = bindings.data();
    check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &modelSetLayout), "Create model descriptor layout");
    VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8},
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes    = sizes;
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &modelPool), "Create model descriptor pool");
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool     = modelPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts        = &modelSetLayout;
    check(vkAllocateDescriptorSets(device, &alloc, &modelSet), "Allocate model descriptors");
    VkDescriptorBufferInfo bufferInfo[] = {
      {modelUniform.buffer, 0, sizeof(SceneUniforms)},
      {modelVertices.buffer, 0, VK_WHOLE_SIZE},
      {modelSkin.buffer, 0, VK_WHOLE_SIZE},
      {modelJoints.buffer, 0, VK_WHOLE_SIZE}};
    std::array<VkDescriptorImageInfo, 9> images{};
    std::array<VkWriteDescriptorSet, 13> writes{};
    for (uint32_t i = 0; i < 13; ++i)
    {
        auto& w           = writes[i];
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = modelSet;
        w.dstBinding      = i;
        w.descriptorCount = 1;
        w.descriptorType  = bindings[i].descriptorType;
        if (i < 2)
            w.pBufferInfo = &bufferInfo[i];
        else if (i >= 11)
            w.pBufferInfo = &bufferInfo[i - 9];
        else
        {
            auto& image       = images[i - 2];
            image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (i == 7)
                image.sampler = modelSampler;
            else
                image.imageView = i == 10  ? modelTextures[0].view
                                  : i == 9 ? backgroundCube.view
                                  : i == 8 ? environmentCube.view
                                           : modelTextures[i - 2].view;
            w.pImageInfo = &image;
        }
    }
    vkUpdateDescriptorSets(device, 13, writes.data(), 0, nullptr);
    modelReady = true;
}
void LoadModel(std::shared_ptr<const ModelData> data) override
{
    WaitIdle();
    destroySwapchain();
    destroyModel();
    Backend::LoadModel(std::move(data));
    resized = true;
}
void destroyModel()
{
    if (modelPool)
    {
        vkDestroyDescriptorPool(device, modelPool, nullptr);
    }
    modelPool = VK_NULL_HANDLE;
    if (modelSetLayout)
    {
        vkDestroyDescriptorSetLayout(device, modelSetLayout, nullptr);
    }
    modelSetLayout = VK_NULL_HANDLE;
    if (modelSampler)
    {
        vkDestroySampler(device, modelSampler, nullptr);
    }
    modelSampler = VK_NULL_HANDLE;
    for (auto& t : modelTextures)
        destroyModelTexture(t);
    destroyModelTexture(environmentCube);
    destroyModelTexture(backgroundCube);
    destroyModelTexture(pathDisplay);
    pathWidth = pathHeight = 0;
    destroyModelBuffer(modelVertices);
    destroyModelBuffer(modelSkin);
    destroyModelBuffer(modelJoints);
    destroyModelBuffer(modelUniform);
    destroyModelBuffer(modelStaging);
    modelReady = false;
}

void UploadPathImage(const ModelImage& image)
{
    check(vkDeviceWaitIdle(device), "Wait path display upload");
    const bool fresh = !pathDisplay.image || pathWidth != image.width || pathHeight != image.height;
    if (fresh)
    {
        destroyModelTexture(pathDisplay);
        modelImage(
          pathDisplay,
          image.width,
          image.height,
          VK_FORMAT_R8G8B8A8_SRGB,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT);
        pathWidth  = image.width;
        pathHeight = image.height;
    }
    modelBuffer(modelStaging, image.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, image.pixels.data());
    check(vkResetCommandBuffer(cmd, 0), "Reset path upload");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(cmd, &begin), "Begin path upload");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                                             = pathDisplay.image;
    barrier.subresourceRange                                  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout     = fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = fresh ? 0 : VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(
      cmd,
      fresh ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent      = {image.width, image.height, 1};
    vkCmdCopyBufferToImage(cmd, modelStaging.buffer, pathDisplay.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
      cmd,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &barrier);
    check(vkEndCommandBuffer(cmd), "End path upload");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "Submit path upload");
    check(vkQueueWaitIdle(queue), "Wait path upload");
    destroyModelBuffer(modelStaging);
    VkDescriptorImageInfo info{VK_NULL_HANDLE, pathDisplay.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet  write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = modelSet;
    write.dstBinding      = 10;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo      = &info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}
