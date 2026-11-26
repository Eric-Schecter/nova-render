struct CullBounds
{
    std::array<float, 4> center{}, extent{};
};
struct CullParameters
{
    float    planes[6][4]{};
    uint32_t instanceCount, unused, dispatchWidth, pad;
};
static_assert(sizeof(CullBounds) == 32 && sizeof(CullParameters) == 112);

bool                               instanceCullSupported = false, instanceCullActive = false;
VkDeviceSize                       instanceCullStorageLimit  = 0;
uint32_t                           instanceCullDispatchLimit = 0, instanceCullBase = 0;
VkDescriptorSetLayout              instanceCullSetLayout{};
VkDescriptorPool                   instanceCullPool{};
VkDescriptorSet                    instanceCullSet{};
VkPipelineLayout                   instanceCullLayout{};
VkPipeline                         instanceCullPipeline{};
ModelBuffer                        cullReset, cullCommands, cullBatches, cullBounds;
std::weak_ptr<const ModelData>     instanceCullModel;
std::vector<VkDrawIndirectCommand> cullLayout;

void createInstanceCullPipeline()
{
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (uint32_t i = 0; i < 4; ++i)
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = 4;
    setInfo.pBindings    = bindings;
    check(
      vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &instanceCullSetLayout),
      "Create instance cull set layout");
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &size;
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &instanceCullPool), "Create instance cull pool");
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool     = instanceCullPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts        = &instanceCullSetLayout;
    check(vkAllocateDescriptorSets(device, &alloc, &instanceCullSet), "Allocate instance cull set");

    const VkPushConstantRange  push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullParameters)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &instanceCullSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &push;
    check(
      vkCreatePipelineLayout(device, &layoutInfo, nullptr, &instanceCullLayout),
      "Create instance cull pipeline layout");

    const auto                  module = shader("instance-cull.comp.spv");
    VkComputePipelineCreateInfo create{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    create.layout       = instanceCullLayout;
    create.stage        = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    create.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    create.stage.module = module;
    create.stage.pName  = "CullInstances";
    const auto result   = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &create, nullptr, &instanceCullPipeline);
    vkDestroyShaderModule(device, module, nullptr);
    check(result, "Create instance cull pipeline");
}

void prepareInstanceCull()
{
    instanceCullActive = instanceCullSupported && mode == RenderMode::Raster && scene.active && model
                         && modelRaster.view == GBufferView::Final && model->skinInputs.empty()
                         && !HasRasterTransparency(model.get()) && !HasSceneOutlines()
                         && GetAntiAliasing().method != AAMethod::TAA && !scene.instances.empty()
                         && scene.draws.size() >= 2
                         && (VkDeviceSize(RasterInstances().size()) + scene.instances.size()) * sizeof(InstanceData)
                              <= instanceCullStorageLimit;
    if (!instanceCullActive)
        return;

    const auto base        = uint32_t(RasterInstances().size());
    const auto sameCommand = [](const auto& a, const auto& b) {
        return a.vertexCount == b.vertexCount && a.instanceCount == b.instanceCount && a.firstVertex == b.firstVertex
               && a.firstInstance == b.firstInstance;
    };
    if (
      instanceCullBase == base && instanceCullModel.lock() == model
      && std::equal(cullLayout.begin(), cullLayout.end(), indirectDraws.begin(), indirectDraws.end(), sameCommand))
        return;
    if (!instanceCullPipeline)
        createInstanceCullPipeline();

    std::vector<uint32_t>   batches(scene.instances.size());
    std::vector<CullBounds> boxes(indirectDraws.size());
    auto                    reset = indirectDraws;
    for (uint32_t i = 0; i < reset.size(); ++i)
    {
        auto&      draw  = reset[i];
        const auto first = draw.firstInstance;
        std::fill_n(batches.begin() + first, draw.instanceCount, i);
        draw.firstInstance += base;
        draw.instanceCount = 0;
        if (!draw.vertexCount)
            continue;
        const auto*          vertices = model->vertices.data() + draw.firstVertex;
        std::array<float, 3> minimum{}, maximum{};
        std::copy_n(vertices->position, 3, minimum.begin());
        maximum        = minimum;
        bool triangles = true;
        for (uint32_t v = 0; v < draw.vertexCount; ++v)
        {
            triangles &= vertices[v].uv1[2] <= .5f;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], vertices[v].position[axis]);
                maximum[axis] = std::max(maximum[axis], vertices[v].position[axis]);
            }
        }
        auto& box = boxes[i];
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            box.center[axis] = (maximum[axis] + minimum[axis]) * .5f;
            box.extent[axis] = (maximum[axis] - minimum[axis]) * .5f;
        }
        // Lines and points expand in clip space. Keep those batches conservative.
        box.extent[3] = triangles ? 1.f : 0.f;
    }
    instanceCullModel.reset();
    auto replace = [&](ModelBuffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage, const void* data) {
        destroyModelBuffer(buffer);
        modelBuffer(buffer, size, usage, data);
    };
    const auto commandBytes = reset.size() * sizeof(VkDrawIndirectCommand);
    replace(cullReset, commandBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, reset.data());
    replace(
      cullCommands,
      commandBytes,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
      nullptr);
    replace(cullBatches, batches.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, batches.data());
    replace(cullBounds, boxes.size() * sizeof(CullBounds), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, boxes.data());
    cullLayout        = indirectDraws;
    instanceCullModel = model;
    instanceCullBase  = base;
}

void encodeInstanceCull(VkCommandBuffer command, const SceneUniforms& uniforms)
{
    if (!instanceCullActive)
        return;
    VkBufferCopy copy{0, 0, cullLayout.size() * sizeof(VkDrawIndirectCommand)};
    vkCmdCopyBuffer(command, cullReset.buffer, cullCommands.buffer, 1, &copy);
    VkMemoryBarrier resetBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    resetBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    resetBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(
      command,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0,
      1,
      &resetBarrier,
      0,
      nullptr,
      0,
      nullptr);

    const VkDescriptorBufferInfo buffers[] = {
      {sceneInstanceBuffer.buffer, 0, VK_WHOLE_SIZE},
      {cullBatches.buffer, 0, VK_WHOLE_SIZE},
      {cullBounds.buffer, 0, VK_WHOLE_SIZE},
      {cullCommands.buffer, 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet writes[4]{};
    for (uint32_t i = 0; i < 4; ++i)
    {
        writes[i]                 = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet          = instanceCullSet;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo     = &buffers[i];
    }
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);

    CullParameters params{};
    for (uint32_t axis = 0; axis < 4; ++axis)
    {
        // Extract clip-space inequalities from the columns of world-to-clip.
        const float* column    = uniforms.viewProjection + axis * 4;
        params.planes[0][axis] = column[3] + column[0];
        params.planes[1][axis] = column[3] - column[0];
        params.planes[2][axis] = column[3] + column[1];
        params.planes[3][axis] = column[3] - column[1];
        params.planes[4][axis] = column[2];
        params.planes[5][axis] = column[3] - column[2];
    }
    params.instanceCount   = uint32_t(scene.instances.size());
    const uint32_t groups  = (params.instanceCount + 63) / 64;
    const uint32_t columns = std::min(groups, instanceCullDispatchLimit);
    params.dispatchWidth   = columns * 64;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, instanceCullPipeline);
    vkCmdBindDescriptorSets(
      command, VK_PIPELINE_BIND_POINT_COMPUTE, instanceCullLayout, 0, 1, &instanceCullSet, 0, nullptr);
    vkCmdPushConstants(command, instanceCullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
    vkCmdDispatch(command, columns, (groups + columns - 1) / columns, 1);

    VkMemoryBarrier drawBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    drawBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    drawBarrier.dstAccessMask =
      VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(
      command,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
      0,
      1,
      &drawBarrier,
      0,
      nullptr,
      0,
      nullptr);
}

void destroyInstanceCull()
{
    destroyModelBuffer(cullReset);
    destroyModelBuffer(cullCommands);
    destroyModelBuffer(cullBatches);
    destroyModelBuffer(cullBounds);
    if (instanceCullPipeline)
        vkDestroyPipeline(device, instanceCullPipeline, nullptr);
    if (instanceCullLayout)
        vkDestroyPipelineLayout(device, instanceCullLayout, nullptr);
    if (instanceCullPool)
        vkDestroyDescriptorPool(device, instanceCullPool, nullptr);
    if (instanceCullSetLayout)
        vkDestroyDescriptorSetLayout(device, instanceCullSetLayout, nullptr);
}
