struct IndirectDraw
{
    UINT                 firstVertex, firstInstance;
    D3D12_DRAW_ARGUMENTS draw;
};
static_assert(sizeof(IndirectDraw) == 24);

const bool                     dxGpuCull     = std::getenv("NOVA_DX12_GPU_CULL") != nullptr;
const bool                     dxIndirect    = dxGpuCull || std::getenv("NOVA_DX12_INDIRECT");
bool                           frameIndirect = false, frameCulled = false, cullDispatched = false;
ComPtr<ID3D12RootSignature>    indirectRoot, cullRoot;
ComPtr<ID3D12CommandSignature> indirectSignature;
ComPtr<ID3D12PipelineState>    cullPipeline;
ComPtr<ID3D12Resource>         directArguments, cullArguments, cullReset, cullInstances, cullBatches, cullBounds;
std::vector<SceneDraw>         indirectLayout;
std::weak_ptr<const ModelData> cullModel;
size_t                         cullSourceCount = 0, cullInstanceCount = 0;

ComPtr<ID3D12Resource> CullBuffer(UINT64 bytes, bool readback = false)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = readback ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width     = bytes;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout                                                                  = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = readback ? D3D12_RESOURCE_FLAG_NONE : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> result;
    checkHR(
      device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result)),
      "Create instance cull buffer");
    return result;
}

void SetupCullPipeline()
{
    D3D12_ROOT_PARAMETER roots[5]{};
    roots[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    roots[0].Constants.Num32BitValues = sizeof(CullParameters) / sizeof(UINT);
    roots[1].ParameterType = roots[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    roots[2].ParameterType = roots[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    roots[3].Descriptor.ShaderRegister = roots[4].Descriptor.ShaderRegister = 1;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 5;
    desc.pParameters   = roots;
    ComPtr<ID3DBlob> rootData, errors;
    checkHR(
      D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &rootData, &errors),
      "Serialize instance cull root signature");
    checkHR(
      device->CreateRootSignature(0, rootData->GetBufferPointer(), rootData->GetBufferSize(), IID_PPV_ARGS(&cullRoot)),
      "Create instance cull root signature");
    const auto                        shader = LoadShader(settings.shaderDirectory, "instance-cull.comp.dxil");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = cullRoot.Get();
    pipelineDesc.CS             = {shader.data(), shader.size()};
    checkHR(
      device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&cullPipeline)), "Create instance cull pipeline");
}

void PrepareIndirect()
{
    frameIndirect = dxIndirect && mode == RenderMode::Raster && !presentedImage && !aaTriangle && model && scene.active
                    && scene.draws.size() >= 2 && !scene.instances.empty() && !HasRasterTransparency(model.get())
                    && modelRaster.pipeline == RasterPipeline::Forward && modelRaster.view == GBufferView::Final
                    && !HasSceneOutlines() && !viewportGrid.enabled
                    && (GetAntiAliasing().method == AAMethod::Off || GetAntiAliasing().method == AAMethod::FXAA);
    frameCulled = frameIndirect && dxGpuCull && model->skinInputs.empty() && scene.instances.size() <= 65535 * 64
                  && RasterInstances().size() + scene.instances.size() <= UINT_MAX;
    if (!frameIndirect || (dxGpuCull && !frameCulled))
    {
        frameIndirect = false;
        return;
    }

    if (indirectRoot.Get() != modelRoot.Get())
    {
        D3D12_INDIRECT_ARGUMENT_DESC arguments[2]{};
        arguments[0].Type                         = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        arguments[0].Constant.RootParameterIndex  = 8;
        arguments[0].Constant.Num32BitValuesToSet = 2;
        arguments[1].Type                         = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        D3D12_COMMAND_SIGNATURE_DESC desc{};
        desc.ByteStride       = sizeof(IndirectDraw);
        desc.NumArgumentDescs = 2;
        desc.pArgumentDescs   = arguments;
        checkHR(
          device->CreateCommandSignature(
            &desc, modelRoot.Get(), IID_PPV_ARGS(indirectSignature.ReleaseAndGetAddressOf())),
          "Create model indirect command signature");
        indirectRoot = modelRoot;
    }

    const auto same = [](const SceneDraw& a, const SceneDraw& b) {
        return a.firstVertex == b.firstVertex && a.vertexCount == b.vertexCount && a.firstInstance == b.firstInstance
               && a.instanceCount == b.instanceCount;
    };
    const bool changed =
      !std::equal(scene.draws.begin(), scene.draws.end(), indirectLayout.begin(), indirectLayout.end(), same);
    if (!changed && (!frameCulled || (cullModel.lock() == model && cullSourceCount == RasterInstances().size()
                                      && cullInstanceCount == scene.instances.size())))
        return;

    indirectLayout = scene.draws;
    std::vector<IndirectDraw> draws;
    draws.reserve(indirectLayout.size());
    for (const auto& draw : indirectLayout)
        draws.push_back({draw.firstVertex, draw.firstInstance, {draw.vertexCount, draw.instanceCount, 0, 0}});

    if (!frameCulled)
    {
        directArguments = uploadBuffer(draws.size() * sizeof(IndirectDraw), draws.data());
        return;
    }

    if (!cullPipeline)
        SetupCullPipeline();
    cullSourceCount   = RasterInstances().size();
    cullInstanceCount = scene.instances.size();
    std::vector<UINT>       batches(cullInstanceCount);
    std::vector<CullBounds> bounds(indirectLayout.size());
    for (UINT i = 0; i < indirectLayout.size(); ++i)
    {
        const auto& draw = indirectLayout[i];
        std::fill_n(batches.begin() + draw.firstInstance, draw.instanceCount, i);
        bounds[i] = InstanceCullBounds(*model, draw.firstVertex, draw.vertexCount);
        draws[i].firstInstance += static_cast<UINT>(cullSourceCount);
        draws[i].draw.InstanceCount = 0;
    }
    const auto bytes = draws.size() * sizeof(IndirectDraw);
    cullReset        = uploadBuffer(bytes, draws.data());
    cullBatches      = uploadBuffer(batches.size() * sizeof(UINT), batches.data());
    cullBounds       = uploadBuffer(bounds.size() * sizeof(CullBounds), bounds.data());
    cullArguments    = CullBuffer(bytes);
    cullInstances    = CullBuffer((cullSourceCount + cullInstanceCount) * sizeof(InstanceData));
    cullDispatched   = false;
    cullModel        = model;
}

void EncodeInstanceCull(ID3D12GraphicsCommandList* list, const SceneUniforms& uniforms, bool measure = false)
{
    if (!frameCulled)
        return;

    const auto shaderRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const auto transition = [list](
                              ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        list->ResourceBarrier(1, &barrier);
    };
    if (cullDispatched)
    {
        transition(cullInstances.Get(), shaderRead, D3D12_RESOURCE_STATE_COPY_DEST);
        transition(cullArguments.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    list->CopyBufferRegion(
      cullInstances.Get(), 0, sceneInstanceBuffer.Get(), 0, cullSourceCount * sizeof(InstanceData));
    list->CopyBufferRegion(cullArguments.Get(), 0, cullReset.Get(), 0, indirectLayout.size() * sizeof(IndirectDraw));
    transition(cullInstances.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(cullArguments.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    auto params          = InstanceCullPlanes(uniforms);
    params.instanceCount = static_cast<UINT>(cullInstanceCount);
    const UINT groups    = (params.instanceCount + 63) / 64;
    params.dispatchWidth = groups * 64;
    list->SetPipelineState(cullPipeline.Get());
    list->SetComputeRootSignature(cullRoot.Get());
    list->SetComputeRoot32BitConstants(0, sizeof(params) / sizeof(UINT), &params, 0);
    list->SetComputeRootUnorderedAccessView(1, cullInstances->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(2, cullBatches->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(3, cullBounds->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(4, cullArguments->GetGPUVirtualAddress());
    if (measure)
        list->EndQuery(drawQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    list->Dispatch(groups, 1, 1);
    if (measure)
        list->EndQuery(drawQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    transition(cullInstances.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, shaderRead);
    transition(cullArguments.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    list->SetPipelineState(modelPipeline.Get());
    cullDispatched = true;
}

D3D12_GPU_VIRTUAL_ADDRESS RasterInstanceAddress() const
{
    return (frameCulled ? cullInstances : sceneInstanceBuffer)->GetGPUVirtualAddress();
}

void ExecuteSceneIndirect(ID3D12GraphicsCommandList* list)
{
    list->ExecuteIndirect(
      indirectSignature.Get(),
      static_cast<UINT>(indirectLayout.size()),
      (frameCulled ? cullArguments : directArguments).Get(),
      0,
      nullptr,
      0);
}

const bool              drawBenchmark = std::getenv("NOVA_DX12_BENCH_OFFSCREEN") != nullptr;
ComPtr<ID3D12QueryHeap> drawQueries;
ComPtr<ID3D12Resource>  drawTimes;
UINT64                  drawFrequency = 0;

void SetupDrawQueries()
{
    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = 4;
    checkHR(device->CreateQueryHeap(&desc, IID_PPV_ARGS(&drawQueries)), "Create draw timestamp heap");
    drawTimes = CullBuffer(4 * sizeof(UINT64), true);
    checkHR(queue->GetTimestampFrequency(&drawFrequency), "Get draw timestamp frequency");
}

void ReportDrawQueries(
  std::chrono::steady_clock::time_point recordStart,
  std::chrono::steady_clock::time_point submitStart,
  std::chrono::steady_clock::time_point submitEnd)
{
    D3D12_RANGE range{0, 4 * sizeof(UINT64)};
    void*       data = nullptr;
    checkHR(drawTimes->Map(0, &range, &data), "Read draw timestamps");
    const auto* times    = static_cast<const UINT64*>(data);
    const auto  interval = [&](UINT start, UINT end) { return (times[end] - times[start]) * 1000.0 / drawFrequency; };
    const auto milliseconds = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
    std::cout << "DX12 draw cpu_record_ms=" << milliseconds(submitStart - recordStart)
              << " cpu_submit_ms=" << milliseconds(submitEnd - submitStart)
              << " gpu_cull_ms=" << (frameCulled ? interval(0, 1) : 0) << " gpu_draw_ms=" << interval(2, 3) << '\n';
    D3D12_RANGE written{0, 0};
    drawTimes->Unmap(0, &written);
}
