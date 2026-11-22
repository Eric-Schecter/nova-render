RendererConfig                         settings;
std::shared_ptr<const ModelData>       gpuModel;
std::shared_ptr<const EnvironmentData> gpuEnvironment;
ComPtr<ID3D12Resource>                environmentCube, environmentUpload, backgroundCube, backgroundUpload, pathDisplay;
uint32_t                              pathWidth = 0, pathHeight = 0;
ComPtr<ID3D12PipelineState>           skyPipeline;
ComPtr<ID3D12Resource> modelVertices, modelUniform, modelDepth, modelSkin, modelJoints, sceneInstanceBuffer;
size_t                 sceneInstanceBytes = 0;
std::array<ComPtr<ID3D12Resource>, 8>  modelTextures, modelUploads;
ComPtr<ID3D12DescriptorHeap>          modelHeap, modelDepthHeap;
ComPtr<ID3D12RootSignature>           modelRoot;
ComPtr<ID3D12PipelineState>           modelPipeline;
ComPtr<ID3D12Resource>                uploadBuffer(UINT64 bytes, const void* source = nullptr)
{
    ComPtr<ID3D12Resource> result;
    D3D12_HEAP_PROPERTIES  heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = bytes;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    checkHR(
      device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&result)),
      "Create model upload buffer");
    if (source)
    {
        void*       mapped = nullptr;
        D3D12_RANGE read{0, 0};
        checkHR(result->Map(0, &read, &mapped), "Map model buffer");
        std::memcpy(mapped, source, static_cast<size_t>(bytes));
        result->Unmap(0, nullptr);
    }
    return result;
}
void UploadEnvironmentCube(
  D3D12_CPU_DESCRIPTOR_HANDLE        handle,
  const std::vector<EnvironmentMip>& mips,
  ComPtr<ID3D12Resource>&            cube,
  ComPtr<ID3D12Resource>&            upload)
{
    const UINT          levels = static_cast<UINT>(mips.size()), count = levels * 6;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = mips[0].size;
    desc.Height           = mips[0].size;
    desc.DepthOrArraySize = 6;
    desc.MipLevels        = static_cast<UINT16>(levels);
    desc.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    checkHR(
      device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&cube)),
      "Create HDR environment cube");
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(count);
    std::vector<UINT>                               rows(count);
    std::vector<UINT64>                             rowSizes(count);
    UINT64                                          total = 0;
    device->GetCopyableFootprints(&desc, 0, count, 0, layouts.data(), rows.data(), rowSizes.data(), &total);
    upload             = uploadBuffer(total);
    void*       mapped = nullptr;
    D3D12_RANGE read{0, 0};
    checkHR(upload->Map(0, &read, &mapped), "Map environment upload");
    for (UINT face = 0; face < 6; ++face)
        for (UINT level = 0; level < levels; ++level)
        {
            const UINT  index = level + face * levels;
            const auto& mip   = mips[level];
            const auto* source =
              reinterpret_cast<const unsigned char*>(mip.pixels.data() + size_t(face) * mip.size * mip.size * 4);
            for (UINT row = 0; row < mip.size; ++row)
                std::memcpy(
                  static_cast<unsigned char*>(mapped) + layouts[index].Offset
                    + size_t(row) * layouts[index].Footprint.RowPitch,
                  source + size_t(row) * mip.size * 8,
                  mip.size * 8);
        }
    upload->Unmap(0, nullptr);
    for (UINT index = 0; index < count; ++index)
    {
        D3D12_TEXTURE_COPY_LOCATION source{}, target{};
        source.pResource        = upload.Get();
        source.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint  = layouts[index];
        target.pResource        = cube.Get();
        target.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        target.SubresourceIndex = index;
        commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = cube.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commands->ResourceBarrier(1, &barrier);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format                  = desc.Format;
    view.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURECUBE;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.TextureCube.MipLevels   = levels;
    device->CreateShaderResourceView(cube.Get(), &view, handle);
}
void UploadModel()
{
    modelVertices = uploadBuffer(
      std::max(sizeof(ModelVertex), model->vertices.size() * sizeof(ModelVertex)),
      model->vertices.empty() ? nullptr : model->vertices.data());
    modelSkin = uploadBuffer(
      std::max(size_t(64), model->skinInputs.size() * sizeof(SkinInput)),
      model->skinInputs.empty() ? nullptr : model->skinInputs.data());
    modelJoints = uploadBuffer(
      std::max(size_t(64), model->jointMatrices.size() * sizeof(JointMatrix)),
      model->jointMatrices.empty() ? nullptr : model->jointMatrices.data());

    modelUniform = uploadBuffer(512);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 38;
    heap.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    checkHR(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&modelHeap)), "Create model texture heap");
    checkHR(allocator->Reset(), "Reset model allocator");
    checkHR(commands->Reset(allocator.Get(), nullptr), "Begin model upload");
    auto       handle = modelHeap->GetCPUDescriptorHandleForHeapStart();
    const auto stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (size_t i = 0; i < modelTextures.size(); ++i)
    {
        const auto          levels = MaterialMipmaps(model->images[i], i);
        const auto&         image  = levels[0];
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = image.width;
        desc.Height           = image.height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = UINT16(levels.size());
        desc.Format           = (i == 0 || i == 4) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        checkHR(
          device->CreateCommittedResource(
            &properties,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&modelTextures[i])),
          "Create model texture");
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(levels.size());
        UINT64                                          total = 0;
        device->GetCopyableFootprints(&desc, 0, UINT(levels.size()), 0, footprints.data(), nullptr, nullptr, &total);
        modelUploads[i]    = uploadBuffer(total);
        void*       mapped = nullptr;
        D3D12_RANGE read{0, 0};
        checkHR(modelUploads[i]->Map(0, &read, &mapped), "Map material mips");
        for (size_t level = 0; level < levels.size(); level++)
        {
            const auto& mip = levels[level];
            const auto& fp  = footprints[level];
            for (UINT y = 0; y < mip.height; y++)
                std::memcpy(
                  static_cast<unsigned char*>(mapped) + fp.Offset + y * fp.Footprint.RowPitch,
                  mip.pixels.data() + size_t(y) * mip.width * 4,
                  mip.width * 4);
        }
        modelUploads[i]->Unmap(0, nullptr);
        for (UINT level = 0; level < levels.size(); level++)
        {
            D3D12_TEXTURE_COPY_LOCATION source{}, target{};
            source.pResource        = modelUploads[i].Get();
            source.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint  = footprints[level];
            target.pResource        = modelTextures[i].Get();
            target.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            target.SubresourceIndex = level;
            commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = modelTextures[i].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commands->ResourceBarrier(1, &barrier);
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format                  = desc.Format;
        view.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels     = UINT(levels.size());
        auto textureHandle           = modelHeap->GetCPUDescriptorHandleForHeapStart();
        textureHandle.ptr += (i < 5 ? i : 8 + i - 5) * stride;
        device->CreateShaderResourceView(modelTextures[i].Get(), &view, textureHandle);
    }
    handle.ptr += 5 * stride;
    const auto& env = environment ? *environment : FallbackEnvironment();
    UploadEnvironmentCube(handle, env.mips, environmentCube, environmentUpload);
    handle.ptr += stride;
    UploadEnvironmentCube(handle, BackgroundMips(env), backgroundCube, backgroundUpload);
    handle.ptr += stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC fallback{};
    fallback.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    fallback.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    fallback.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    fallback.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(modelTextures[0].Get(), &fallback, handle);
    checkHR(commands->Close(), "End model upload");
    ID3D12CommandList* lists[] = {commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    WaitIdle();
    environmentUpload.Reset();
    backgroundUpload.Reset();
    for (auto& upload : modelUploads)
        upload.Reset();
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    auto&                  range = ranges[0];
    range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors     = 8;
    range.BaseShaderRegister = 1;
    ranges[1]                    = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 28, 0, 8};
    D3D12_ROOT_PARAMETER params[10]{};
    D3D12_DESCRIPTOR_RANGE gRanges[2]{};
    auto&                  gRange                 = gRanges[0];
    gRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    gRange.NumDescriptors     = 8;
    gRange.BaseShaderRegister = 17;
    gRanges[1]                                    = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 31, 0, 8};
    params[9].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[9].DescriptorTable                     = {2, gRanges};
    params[9].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    params[5].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[5].Descriptor.ShaderRegister           = 11;
    params[5].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    params[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister           = 0;
    params[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister           = 0;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_VERTEX;
    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 2;
    params[2].DescriptorTable.pDescriptorRanges   = ranges;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    for (UINT i = 0; i < 2; ++i)
    {
        params[3 + i].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[3 + i].Descriptor.ShaderRegister = 9 + i;
        params[3 + i].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    }
    params[6].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[6].Descriptor.ShaderRegister = 15;
    params[6].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
    params[7].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[7].Descriptor.ShaderRegister = 16;
    params[7].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[8].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[8].Constants.ShaderRegister  = 1;
    params[8].Constants.Num32BitValues  = 2;
    params[8].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter   = D3D12_FILTER_ANISOTROPIC;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxAnisotropy                                  = 8;
    sampler.ComparisonFunc                                 = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD                                         = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility                               = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters     = 10;
    rootDesc.pParameters       = params;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers   = &sampler;
    ComPtr<ID3DBlob> signature, messages;
    checkHR(
      D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &messages),
      "Serialize model root");
    checkHR(
      device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&modelRoot)),
      "Create model root");
    auto vert = LoadShader(settings.shaderDirectory, "model.vert.dxil"),
         frag = LoadShader(settings.shaderDirectory, "model.frag.dxil");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC info{};
    info.pRootSignature                     = modelRoot.Get();
    info.VS                                 = {vert.data(), vert.size()};
    info.PS                                 = {frag.data(), frag.size()};
    info.RasterizerState.FillMode           = D3D12_FILL_MODE_SOLID;
    info.RasterizerState.CullMode           = D3D12_CULL_MODE_NONE;
    info.RasterizerState.DepthClipEnable    = TRUE;
    auto& blend                             = info.BlendState.RenderTarget[0];
    blend.SrcBlend                          = D3D12_BLEND_ONE;
    blend.DestBlend                         = D3D12_BLEND_ZERO;
    blend.BlendOp                           = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha                     = D3D12_BLEND_ONE;
    blend.DestBlendAlpha                    = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha                      = D3D12_BLEND_OP_ADD;
    blend.LogicOp                           = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask             = D3D12_COLOR_WRITE_ENABLE_ALL;
    info.DepthStencilState.DepthEnable      = TRUE;
    info.DepthStencilState.DepthWriteMask   = D3D12_DEPTH_WRITE_MASK_ALL;
    info.DepthStencilState.DepthFunc        = D3D12_COMPARISON_FUNC_LESS;
    info.DepthStencilState.StencilEnable    = FALSE;
    info.DepthStencilState.StencilReadMask  = D3D12_DEFAULT_STENCIL_READ_MASK;
    info.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    info.DepthStencilState.FrontFace        = {
      D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
    info.DepthStencilState.BackFace = info.DepthStencilState.FrontFace;
    info.SampleMask                 = UINT_MAX;
    info.SampleDesc.Count           = 1;
    info.PrimitiveTopologyType      = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    info.NumRenderTargets           = 1;
    info.RTVFormats[0]              = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    info.DSVFormat                  = DXGI_FORMAT_D32_FLOAT;
    checkHR(device->CreateGraphicsPipelineState(&info, IID_PPV_ARGS(&modelPipeline)), "Create model pipeline");
    if (model)
    {
        const auto skyVert                    = LoadShader(settings.shaderDirectory, "skybox.vert.dxil"),
                   skyFrag                    = LoadShader(settings.shaderDirectory, "skybox.frag.dxil");
        info.VS                               = {skyVert.data(), skyVert.size()};
        info.PS                               = {skyFrag.data(), skyFrag.size()};
        info.DepthStencilState.DepthEnable    = FALSE;
        info.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        checkHR(device->CreateGraphicsPipelineState(&info, IID_PPV_ARGS(&skyPipeline)), "Create skybox pipeline");
    }
    gpuEnvironment = environment;
    gpuModel       = model;
}
void ModelDepth()
{
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = 1;
    if (!modelDepthHeap)
        checkHR(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&modelDepthHeap)), "Create depth heap");
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = width;
    desc.Height           = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format             = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1;
    checkHR(
      device->CreateCommittedResource(
        &properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&modelDepth)),
      "Create model depth");
    device->CreateDepthStencilView(modelDepth.Get(), nullptr, modelDepthHeap->GetCPUDescriptorHandleForHeapStart());
}

void UploadPathImage(const ModelImage& image)
{
    WaitIdle();
    const bool fresh = !pathDisplay || pathWidth != image.width || pathHeight != image.height;
    if (fresh)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = image.width;
        desc.Height           = image.height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        desc.SampleDesc.Count = 1;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        checkHR(
          device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pathDisplay)),
          "Create path display");
        pathWidth  = image.width;
        pathHeight = image.height;
    }
    const auto                         desc = pathDisplay->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT                               rows;
    UINT64                             rowSize, total;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowSize, &total);
    auto        upload = uploadBuffer(total);
    void*       mapped;
    D3D12_RANGE empty{0, 0};
    checkHR(upload->Map(0, &empty, &mapped), "Map path display upload");
    for (UINT row = 0; row < image.height; ++row)
        std::memcpy(
          static_cast<unsigned char*>(mapped) + footprint.Offset + size_t(row) * footprint.Footprint.RowPitch,
          image.pixels.data() + size_t(row) * image.width * 4,
          image.width * 4);
    upload->Unmap(0, nullptr);
    checkHR(allocator->Reset(), "Reset path upload allocator");
    checkHR(commands->Reset(allocator.Get(), nullptr), "Begin path display upload");
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = pathDisplay.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (!fresh)
    {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        commands->ResourceBarrier(1, &barrier);
    }
    D3D12_TEXTURE_COPY_LOCATION source{}, target{};
    source.pResource       = upload.Get();
    source.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    target.pResource       = pathDisplay.Get();
    target.Type            = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commands->ResourceBarrier(1, &barrier);
    checkHR(commands->Close(), "End path display upload");
    ID3D12CommandList* list = commands.Get();
    queue->ExecuteCommandLists(1, &list);
    WaitIdle();
    auto handle = modelHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += SIZE_T(7) * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    view.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(pathDisplay.Get(), &view, handle);
}

void BindRasterGPUOutput()
{
    auto handle = modelHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += SIZE_T(7) * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
    view.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels     = 1;
    device->CreateShaderResourceView(static_cast<ID3D12Resource*>(rasterAA.device->GPUOutput()), &view, handle);
}
