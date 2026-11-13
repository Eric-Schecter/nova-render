RendererConfig                        settings;
const ModelData*                      gpuModel = nullptr;
ComPtr<ID3D12Resource>                modelVertices, modelUniform, modelDepth;
std::array<ComPtr<ID3D12Resource>, 5> modelTextures, modelUploads;
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
void UploadModel()
{
    modelVertices = uploadBuffer(model->vertices.size() * sizeof(ModelVertex), model->vertices.data());
    modelUniform  = uploadBuffer(256);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 5;
    heap.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    checkHR(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&modelHeap)), "Create model texture heap");
    checkHR(allocator->Reset(), "Reset model allocator");
    checkHR(commands->Reset(allocator.Get(), nullptr), "Begin model upload");
    auto       handle = modelHeap->GetCPUDescriptorHandleForHeapStart();
    const auto stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (size_t i = 0; i < 5; ++i)
    {
        const auto&         image = model->images[i];
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = image.width;
        desc.Height           = image.height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
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
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT                               rows    = 0;
        UINT64                             rowSize = 0, total = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowSize, &total);
        modelUploads[i]    = uploadBuffer(total);
        void*       mapped = nullptr;
        D3D12_RANGE read{0, 0};
        checkHR(modelUploads[i]->Map(0, &read, &mapped), "Map model texture");
        for (UINT row = 0; row < rows; ++row)
            std::memcpy(
              static_cast<unsigned char*>(mapped) + footprint.Offset + row * footprint.Footprint.RowPitch,
              image.pixels.data() + row * image.width * 4,
              image.width * 4);
        modelUploads[i]->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource       = modelUploads[i].Get();
        source.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION target{};
        target.pResource = modelTextures[i].Get();
        target.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
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
        view.Texture2D.MipLevels     = 1;
        device->CreateShaderResourceView(modelTextures[i].Get(), &view, handle);
        handle.ptr += stride;
    }
    checkHR(commands->Close(), "End model upload");
    ID3D12CommandList* lists[] = {commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    WaitIdle();
    for (auto& upload : modelUploads)
        upload.Reset();
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors     = 5;
    range.BaseShaderRegister = 1;
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister           = 0;
    params[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].Descriptor.ShaderRegister           = 0;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_VERTEX;
    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges   = &range;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter   = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxAnisotropy                                  = 1;
    sampler.ComparisonFunc                                 = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD                                         = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility                               = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters     = 3;
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
    gpuModel = model.get();
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
