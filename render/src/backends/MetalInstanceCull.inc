bool                           metalIndirect = false, metalGpuCull = false, frameIndirect = false, frameCulled = false;
id<MTLIndirectCommandBuffer>   indirectCommands;
std::vector<SceneDraw>         indirectLayout;
std::weak_ptr<const ModelData> cullModel;
uint32_t                       cullBase = 0;
id<MTLBuffer>                  cullDraws, cullBatches, cullBounds, cullIcbArguments;
std::vector<MTLDrawPrimitivesIndirectArguments> cullReset;
id<MTLComputePipelineState>                     cullPipeline, encodeCommandsPipeline;
id<MTLArgumentEncoder>                          icbArgumentEncoder;

id<MTLRenderPipelineState> icbModelPipelines[2][2];
id<MTLArgumentEncoder>     icbFragmentEncoders[2];
id<MTLBuffer>              icbFragmentArguments[2], icbSceneUniforms;

id<MTLLibrary> LoadIcbLibrary(const char* file)
{
    const auto bytes = LoadShader(settings.shaderDirectory, file);
    NSData*    data  = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    auto       blob  = dispatch_data_create(data.bytes, data.length, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
      (void)data;
    });
    NSError*   error = nil;
    auto       library = [device newLibraryWithData:blob error:&error];
    if (!library)
        throw metalError(file, error);
    return library;
}

void BindIndirectModel(id<MTLRenderCommandEncoder> encoder, const SceneUniforms& uniforms, bool offscreen)
{
    const bool indexed         = UsesIndexedMaterials();
    auto&      pipeline        = icbModelPipelines[offscreen][indexed];
    auto&      argumentEncoder = icbFragmentEncoders[indexed];
    auto&      arguments       = icbFragmentArguments[indexed];
    if (!pipeline)
    {
        auto vertexLibrary    = LoadIcbLibrary("model.metallib");
        auto fragmentLibrary  = LoadIcbLibrary(indexed ? "model-indexed-icb.metallib" : "model-icb.metallib");
        auto desc             = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction   = [vertexLibrary newFunctionWithName:@"vertexMain"];
        desc.fragmentFunction = [fragmentLibrary newFunctionWithName:@"fragmentMain"];
        desc.colorAttachments[0].pixelFormat = offscreen ? MTLPixelFormatRGBA16Float : layer.pixelFormat;
        desc.depthAttachmentPixelFormat      = MTLPixelFormatDepth32Float;
        desc.supportIndirectCommandBuffers   = YES;
        NSError* error                       = nil;
        pipeline                             = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline)
            throw metalError("Create Metal ICB model pipeline", error);
        if (!argumentEncoder)
        {
            argumentEncoder = [desc.fragmentFunction newArgumentEncoderWithBufferIndex:0];
            arguments = [device newBufferWithLength:argumentEncoder.encodedLength options:MTLResourceStorageModeShared];
            if (!arguments)
                throw std::runtime_error("Allocate Metal ICB fragment bindings failed");
        }
    }
    if (!icbSceneUniforms)
        icbSceneUniforms = [device newBufferWithLength:sizeof(SceneUniforms) options:MTLResourceStorageModeShared];
    if (!icbSceneUniforms)
        throw std::runtime_error("Allocate Metal ICB scene uniforms failed");

    std::memcpy(icbSceneUniforms.contents, &uniforms, sizeof(uniforms));
    [argumentEncoder setArgumentBuffer:arguments offset:0];
    [argumentEncoder setBuffer:icbSceneUniforms offset:0 atIndex:0];
    [argumentEncoder setBuffer:sceneInstanceBuffer offset:0 atIndex:14];
    [argumentEncoder setSamplerState:modelSampler atIndex:7];
    [argumentEncoder setTexture:environmentTexture atIndex:8];
    [encoder useResource:icbSceneUniforms usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
    [encoder useResource:sceneInstanceBuffer usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
    [encoder useResource:environmentTexture usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
    for (NSUInteger i = 0; i < modelTextures.size(); ++i)
    {
        if (indexed && i >= 1 && i <= 3)
            continue;
        [argumentEncoder setTexture:modelTextures[i] atIndex:(i < 5 ? 2 + i : 28 + i - 5)];
        [encoder useResource:modelTextures[i] usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
    }
    [encoder setVertexBuffer:icbSceneUniforms offset:0 atIndex:0];
    [encoder setFragmentBuffer:arguments offset:0 atIndex:0];
    [encoder setRenderPipelineState:pipeline];
}

using DrawClock = std::chrono::steady_clock;
id<MTLCounterSampleBuffer> drawCounters;
MTLTimestamp               frameCpuTick = 0, frameGpuTick = 0;
double                     recordMs = 0, submitMs = 0;
bool                       drawMetricsPending = false;
enum DrawSample : NSUInteger
{
    CullStart,
    CullEnd,
    EncodeStart,
    EncodeEnd,
    VertexStart,
    VertexEnd,
    FragmentStart,
    FragmentEnd,
    DrawSampleCount
};

void InitializeIndirect()
{
#if TARGET_OS_OSX
    metalGpuCull  = std::getenv("NOVA_METAL_GPU_CULL") != nullptr;
    metalIndirect = [device supportsFamily:MTLGPUFamilyApple5] && (metalGpuCull || std::getenv("NOVA_METAL_INDIRECT"));
    std::error_code error;
    if (metalIndirect && !settings.shaderDirectory.empty())
        for (const auto* file : {"model-icb.metallib", "model-indexed-icb.metallib"})
            metalIndirect &= std::filesystem::is_regular_file(settings.shaderDirectory / file, error);
    metalGpuCull =
      metalIndirect && metalGpuCull
      && (settings.shaderDirectory.empty() || std::filesystem::is_regular_file(settings.shaderDirectory / "instance-cull.metallib", error));
    if (std::getenv("NOVA_METAL_DRAW_TRACE"))
        std::cout << "Metal draw mode: "
                  << (metalGpuCull    ? "GPU culled ICB"
                      : metalIndirect ? "CPU encoded ICB"
                                      : "direct")
                  << '\n';
    if (
      std::getenv("NOVA_METAL_DRAW_METRICS") && [device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])
        for (id<MTLCounterSet> set in device.counterSets)
            if ([set.name isEqualToString:MTLCommonCounterSetTimestamp])
            {
                auto desc          = [MTLCounterSampleBufferDescriptor new];
                desc.counterSet    = set;
                desc.storageMode   = MTLStorageModeShared;
                desc.sampleCount   = DrawSampleCount;
                NSError* loadError = nil;
                drawCounters       = [device newCounterSampleBufferWithDescriptor:desc error:&loadError];
                if (!drawCounters)
                    throw metalError("Create Metal draw counters", loadError);
                break;
            }
#endif
}

void SetupCullPipelines()
{
    const auto bytes = LoadShader(settings.shaderDirectory, "instance-cull.metallib");
    NSData*    data  = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    auto       blob  = dispatch_data_create(data.bytes, data.length, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
      (void)data;
    });
    NSError*   error = nil;
    auto       library = [device newLibraryWithData:blob error:&error];
    if (!library)
        throw metalError("Load Metal instance cull shaders", error);
    auto cull    = [library newFunctionWithName:@"computeMain"];
    auto encode  = [library newFunctionWithName:@"encodeInstanceCommands"];
    cullPipeline = [device newComputePipelineStateWithFunction:cull error:&error];
    if (!cullPipeline)
        throw metalError("Create Metal instance cull pipeline", error);
    encodeCommandsPipeline = [device newComputePipelineStateWithFunction:encode error:&error];
    icbArgumentEncoder     = [encode newArgumentEncoderWithBufferIndex:1];
    if (!encodeCommandsPipeline || !icbArgumentEncoder)
        throw metalError("Create Metal ICB encoding pipeline", error);
    cullIcbArguments =
      [device newBufferWithLength:icbArgumentEncoder.encodedLength options:MTLResourceStorageModeShared];
    if (!cullIcbArguments)
        throw std::runtime_error("Allocate Metal ICB arguments failed");
}

void PrepareIndirect()
{
    frameIndirect = metalIndirect && mode == RenderMode::Raster && model && scene.active && scene.draws.size() >= 2
                    && !scene.instances.empty() && !HasRasterTransparency(model.get())
                    && modelRaster.pipeline == RasterPipeline::Forward && !HasSceneOutlines()
                    && (GetAntiAliasing().method == AAMethod::Off || GetAntiAliasing().method == AAMethod::FXAA);
    const uint64_t compactCount = uint64_t(RasterInstances().size()) + scene.instances.size();
    frameCulled = frameIndirect && metalGpuCull && modelRaster.view == GBufferView::Final && model->skinInputs.empty()
                  && !HasSceneOutlines() && GetAntiAliasing().method != AAMethod::TAA
                  && compactCount * sizeof(InstanceData) <= device.maxBufferLength;
    // A GPU ICB is rewritten in the cull pass. Unsupported cull cases use the direct path.
    if (!frameIndirect || (metalGpuCull && !frameCulled))
    {
        frameIndirect = false;
        return;
    }
    const auto sameDraw = [](const SceneDraw& a, const SceneDraw& b) {
        return a.firstVertex == b.firstVertex && a.vertexCount == b.vertexCount && a.firstInstance == b.firstInstance
               && a.instanceCount == b.instanceCount;
    };
    const bool changed =
      !indirectCommands
      || !std::equal(scene.draws.begin(), scene.draws.end(), indirectLayout.begin(), indirectLayout.end(), sameDraw);
    if (changed)
    {
        auto desc                 = [MTLIndirectCommandBufferDescriptor new];
        desc.commandTypes         = MTLIndirectCommandTypeDraw;
        desc.inheritBuffers       = YES;
        desc.inheritPipelineState = YES;
        indirectCommands =
          [device newIndirectCommandBufferWithDescriptor:desc maxCommandCount:scene.draws.size() options:0];
        if (!indirectCommands)
            throw std::runtime_error("Allocate Metal indirect draw buffer failed");
        indirectLayout = scene.draws;
    }
    if (!frameCulled)
    {
        if (changed)
            for (size_t i = 0; i < indirectLayout.size(); ++i)
            {
                const auto& draw = indirectLayout[i];
                if (draw.instanceCount)
                    [[indirectCommands indirectRenderCommandAtIndex:i] drawPrimitives:MTLPrimitiveTypeTriangle
                                                                          vertexStart:draw.firstVertex
                                                                          vertexCount:draw.vertexCount
                                                                        instanceCount:draw.instanceCount
                                                                         baseInstance:draw.firstInstance];
            }
        return;
    }

    const auto base = uint32_t(RasterInstances().size());
    if (!changed && cullModel.lock() == model && cullBase == base)
        return;
    if (!cullPipeline)
        SetupCullPipelines();
    std::vector<uint32_t>   batches(scene.instances.size());
    std::vector<CullBounds> boxes(indirectLayout.size());
    cullReset.resize(indirectLayout.size());
    for (uint32_t i = 0; i < indirectLayout.size(); ++i)
    {
        const auto& draw = indirectLayout[i];
        std::fill_n(batches.begin() + draw.firstInstance, draw.instanceCount, i);
        boxes[i]     = InstanceCullBounds(*model, draw.firstVertex, draw.vertexCount);
        cullReset[i] = {draw.vertexCount, 0, draw.firstVertex, draw.firstInstance + base};
    }
    cullDraws =
      [device newBufferWithLength:cullReset.size() * sizeof(cullReset[0]) options:MTLResourceStorageModeShared];
    cullBatches = [device newBufferWithBytes:batches.data()
                                      length:batches.size() * sizeof(batches[0])
                                     options:MTLResourceStorageModeShared];
    cullBounds  = [device newBufferWithBytes:boxes.data()
                                     length:boxes.size() * sizeof(boxes[0])
                                    options:MTLResourceStorageModeShared];
    if (!cullDraws || !cullBatches || !cullBounds)
        throw std::runtime_error("Allocate Metal instance cull buffers failed");
    [icbArgumentEncoder setArgumentBuffer:cullIcbArguments offset:0];
    [icbArgumentEncoder setIndirectCommandBuffer:indirectCommands atIndex:0];
    cullModel = model;
    cullBase  = base;
}

void EncodeInstanceCull(id<MTLCommandBuffer> command, const SceneUniforms& uniforms, bool measure = false)
{
    if (!frameCulled)
        return;
    std::memcpy(cullDraws.contents, cullReset.data(), cullReset.size() * sizeof(cullReset[0]));
    auto params          = InstanceCullPlanes(uniforms);
    params.instanceCount = uint32_t(scene.instances.size());
    params.dispatchWidth = ((params.instanceCount + 63) / 64) * 64;
    auto computeEncoder  = [&](DrawSample start, DrawSample end) {
        if (!measure)
            return [command computeCommandEncoder];
        auto pass                         = [MTLComputePassDescriptor computePassDescriptor];
        auto samples                      = pass.sampleBufferAttachments[0];
        samples.sampleBuffer              = drawCounters;
        samples.startOfEncoderSampleIndex = start;
        samples.endOfEncoderSampleIndex   = end;
        return [command computeCommandEncoderWithDescriptor:pass];
    };
    auto cull = computeEncoder(CullStart, CullEnd);
    [cull setComputePipelineState:cullPipeline];
    [cull setBuffer:sceneInstanceBuffer offset:0 atIndex:0];
    [cull setBuffer:cullBatches offset:0 atIndex:1];
    [cull setBuffer:cullBounds offset:0 atIndex:2];
    [cull setBuffer:cullDraws offset:0 atIndex:3];
    [cull setBytes:&params length:sizeof(params) atIndex:4];
    [cull dispatchThreads:MTLSizeMake(params.instanceCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [cull endEncoding];

    auto reset = [command blitCommandEncoder];
    [reset resetCommandsInBuffer:indirectCommands withRange:NSMakeRange(0, indirectLayout.size())];
    [reset endEncoding];
    auto encode = computeEncoder(EncodeStart, EncodeEnd);
    [encode setComputePipelineState:encodeCommandsPipeline];
    [encode setBuffer:cullDraws offset:0 atIndex:0];
    [encode setBuffer:cullIcbArguments offset:0 atIndex:1];
    [encode useResource:indirectCommands usage:MTLResourceUsageWrite];
    [encode dispatchThreads:MTLSizeMake(indirectLayout.size(), 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
    [encode endEncoding];
}

void DrawScene(id<MTLRenderCommandEncoder> e, const AAPass& pass)
{
    if (frameIndirect && !pass.outlineGroup)
        [e executeCommandsInBuffer:indirectCommands withRange:NSMakeRange(0, indirectLayout.size())];
    else
        DrawRasterInstances(pass, [&](const SceneDraw& draw, uint32_t count, uint32_t first) {
            [e drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:draw.firstVertex
                  vertexCount:draw.vertexCount
                instanceCount:count
                 baseInstance:first];
        });
}

void PrepareDrawSamples(MTLRenderPassDescriptor* pass)
{
    [device sampleTimestamps:&frameCpuTick gpuTimestamp:&frameGpuTick];
    auto attachment                       = pass.sampleBufferAttachments[0];
    attachment.sampleBuffer               = drawCounters;
    attachment.startOfVertexSampleIndex   = VertexStart;
    attachment.endOfVertexSampleIndex     = VertexEnd;
    attachment.startOfFragmentSampleIndex = FragmentStart;
    attachment.endOfFragmentSampleIndex   = FragmentEnd;
}

void ReportDrawSamples()
{
    if (!drawMetricsPending)
        return;
    drawMetricsPending = false;
    @autoreleasepool
    {
        MTLTimestamp cpu = 0, gpu = 0;
        [device sampleTimestamps:&cpu gpuTimestamp:&gpu];
        const auto range = frameCulled ? NSMakeRange(0, DrawSampleCount) : NSMakeRange(VertexStart, 4);
        NSData*    data  = [drawCounters resolveCounterRange:range];
        if (
          data.length != range.length * sizeof(MTLCounterResultTimestamp) || cpu <= frameCpuTick || gpu <= frameGpuTick)
            return;
        const auto*  samples  = static_cast<const MTLCounterResultTimestamp*>(data.bytes);
        const double scale    = double(cpu - frameCpuTick) / double(gpu - frameGpuTick) / 1e6;
        const auto   interval = [&](DrawSample a, DrawSample b) {
            const auto start = samples[a - range.location].timestamp;
            const auto end   = samples[b - range.location].timestamp;
            if (!start || end < start || end == UINT64_MAX)
                return -1.0;
            return double(end - start) * scale;
        };
        size_t submitted = scene.instances.size();
        if (frameCulled)
        {
            const auto* draws = static_cast<const MTLDrawPrimitivesIndirectArguments*>(cullDraws.contents);
            submitted         = 0;
            for (size_t i = 0; i < indirectLayout.size(); ++i)
                submitted += draws[i].instanceCount;
        }
        std::cout << "Metal draw metrics: record_ms=" << recordMs << " submit_ms=" << submitMs
                  << " gpu_cull_ms=" << (frameCulled ? interval(CullStart, CullEnd) : 0)
                  << " gpu_icb_ms=" << (frameCulled ? interval(EncodeStart, EncodeEnd) : 0)
                  << " gpu_draw_ms=" << interval(VertexStart, FragmentEnd)
                  << " gpu_ms=" << (lastCommand.GPUEndTime - lastCommand.GPUStartTime) * 1000
                  << " submitted=" << submitted << '\n';
    }
}
