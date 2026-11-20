#include "../Backend.hpp"
#include "../ShaderData.hpp"
#ifdef NOVA_HAS_UI
#include <imgui_impl_metal.h>
#endif
#include <SDL3/SDL_metal.h>
#include <SDL3/SDL_error.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace render::detail
{
    static std::runtime_error metalError(const char* operation, NSError* error)
    {
        return std::runtime_error(
          std::string(operation) + ": "
          + (error ? error.localizedDescription.UTF8String : "Metal resource unavailable"));
    }
    class MetalBackend final : public Backend
    {
      public:
        ~MetalBackend() override
        {
            @autoreleasepool
            {
                if (lastCommand)
                    [lastCommand waitUntilCompleted];
#ifdef NOVA_HAS_UI
                if (uiRenderer)
                {
                    parameterPanel->Activate();
                    ImGui_ImplMetal_Shutdown();
                }
#endif
                if (metalView)
                    SDL_Metal_DestroyView(metalView);
            }
        }
        void Initialize(PresentationWindow window, const RendererConfig& config)
        {
            @autoreleasepool
            {
                settings = config;
                device   = MTLCreateSystemDefaultDevice();
                if (!device)
                    throw std::runtime_error("No Metal device available");
                queue = [device newCommandQueue];
                if (!queue)
                    throw std::runtime_error("Create Metal queue failed");
                metalView = SDL_Metal_CreateView(window.sdl);
                if (!metalView)
                    throw std::runtime_error(SDL_GetError());
                layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(metalView);
                if (!layer)
                    throw std::runtime_error("SDL Metal view has no CAMetalLayer");
                layer.device          = device;
                layer.pixelFormat     = MTLPixelFormatBGRA8Unorm_sRGB;
                layer.framebufferOnly = YES;
                layer.opaque          = YES;
                const auto      bytes    = LoadShader(config.shaderDirectory, "triangle.metallib");
                NSData*         data     = [NSData dataWithBytes:bytes.data() length:bytes.size()];
                dispatch_data_t shaderData =
                  dispatch_data_create(data.bytes, data.length, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
                    (void)data;
                  });
                NSError*       error   = nil;
                id<MTLLibrary> library = [device newLibraryWithData:shaderData error:&error];
                if (!library)
                    throw metalError("Load triangle.metallib", error);
                MTLRenderPipelineDescriptor* desc    = [MTLRenderPipelineDescriptor new];
                desc.vertexFunction                  = [library newFunctionWithName:@"vertexMain"];
                desc.fragmentFunction                = [library newFunctionWithName:@"fragmentMain"];
                desc.colorAttachments[0].pixelFormat = layer.pixelFormat;
                pipeline                             = [device newRenderPipelineStateWithDescriptor:desc error:&error];
                if (!pipeline)
                    throw metalError("Create Metal pipeline", error);
                std::cout << "GPU: " << device.name.UTF8String << '\n';
                std::cout << "Metal command errors are counted; API validation is controlled by MTL_DEBUG_LAYER.\n";
            }
        }
        void Resize(uint32_t newWidth, uint32_t newHeight) override
        {
            if (width == newWidth && height == newHeight)
                return;
            width   = newWidth;
            height  = newHeight;
            resized = true;
        }
        FrameResult DrawFrame() override
        {
            if (!width || !height)
                return FrameResult::Skipped;
            const auto earlyViewport = SceneViewport(width, height);
            if (!earlyViewport.width || !earlyViewport.height)
                return FrameResult::Skipped;
            @autoreleasepool
            {
                WaitIdle(); // One frame in flight, matching the other backends.
                if (resized)
                {
                    layer.drawableSize  = CGSizeMake(width, height);
                    resized             = false;
                    std::cout << "Metal drawable: " << width << 'x' << height << '\n';
                }
                if (model && (gpuModel != model || gpuEnvironment != environment))
                    UploadModel();
                if (model && (!depth || depth.width != width || depth.height != height))
                {
                    MTLTextureDescriptor* desc =
                      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                         width:width
                                                                        height:height
                                                                     mipmapped:NO];
                    desc.storageMode = MTLStorageModePrivate;
                    desc.usage       = MTLTextureUsageRenderTarget;
                    depth            = [device newTextureWithDescriptor:desc];
                    if (!depth)
                        throw std::runtime_error("Create model depth texture failed");
                }
                id<CAMetalDrawable> drawable = [layer nextDrawable];
                if (!drawable)
                    return FrameResult::Skipped;
                id<MTLCommandBuffer> command = [queue commandBuffer];
                if (!command)
                    throw std::runtime_error("Create Metal command buffer failed");
                MTLRenderPassDescriptor* pass        = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture     = drawable.texture;
                pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor  = MTLClearColorMake(0.025, 0.035, 0.055, 1.0);
                if (model)
                {
                    pass.depthAttachment.texture     = depth;
                    pass.depthAttachment.loadAction  = MTLLoadActionClear;
                    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
                    pass.depthAttachment.clearDepth  = 1.0;
                }
#ifdef NOVA_HAS_UI
                if (panelEnabled)
                {
                    parameterPanel->Activate();
                    if (!uiRenderer)
                    {
                        if (!ImGui_ImplMetal_Init(device))
                            throw std::runtime_error("Initialize Metal UI failed");
                        uiRenderer = true;
                    }
                    parameterPanel->SetSRGBOutput(true);
                    ImGui_ImplMetal_NewFrame(pass);
                    DrawParameterPanel();
                }
#endif
                if (model && !model->skinInputs.empty())
                    std::memcpy(
                      jointPalette.contents,
                      model->jointMatrices.data(),
                      model->jointMatrices.size() * sizeof(JointMatrix));
                const auto clear                    = ClearColorForTarget(true);
                pass.colorAttachments[0].clearColor = MTLClearColorMake(clear[0], clear[1], clear[2], clear[3]);
                size_t instanceBytes = std::max(size_t(192), RasterInstances().size() * sizeof(InstanceData));
                if (!sceneInstanceBuffer || sceneInstanceBuffer.length < instanceBytes)
                    sceneInstanceBuffer = [device newBufferWithLength:instanceBytes
                                                              options:MTLResourceStorageModeShared];
                if (!RasterInstances().empty())
                    std::memcpy(
                      sceneInstanceBuffer.contents,
                      RasterInstances().data(),
                      RasterInstances().size() * sizeof(InstanceData));
                const auto rectangle  = SceneViewport(width, height);
                const auto sceneWidth = rectangle.width, sceneHeight = rectangle.height;
                if (!sceneWidth || !sceneHeight)
                    return FrameResult::Skipped;
                const auto* path     = DrawPathFrame(sceneWidth, sceneHeight);
                const bool  gpuFrame = path && path->pixels.empty() && rasterAA.lastGPUFrame;
                if (path && !gpuFrame)
                    UploadPathImage(*path);
                id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
                if (!encoder)
                    throw std::runtime_error("Create Metal render encoder failed");
                [encoder setRenderPipelineState:model ? modelPipeline : pipeline];
                [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
                [encoder setViewport:MTLViewport{
                                       double(rectangle.x),
                                       double(rectangle.y),
                                       double(sceneWidth),
                                       double(sceneHeight),
                                       0,
                                       1}];
                [encoder setScissorRect:MTLScissorRect{rectangle.x, rectangle.y, sceneWidth, sceneHeight}];
                [encoder setCullMode:MTLCullModeNone];
                if (model)
                {
                    auto uniforms = MakeSceneUniforms(
                      *model,
                      GetViewCamera(),
                      float(sceneWidth) / sceneHeight,
                      lighting,
                      directionalLight,
                      environment.get(),
                      environmentSettings);
                    uniforms.skyParams[2]      = gpuFrame ? 2.f : path ? 1.0f : 0.0f;
                    uniforms.lightDirection[3] = MaterialMode();
                    ApplyRasterSettings(uniforms);
                    [encoder setVertexBuffer:sceneInstanceBuffer offset:0 atIndex:14];
                    [encoder setFragmentBuffer:sceneInstanceBuffer offset:0 atIndex:14];
                    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:0];
                    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
                    [encoder setVertexBuffer:vertices offset:0 atIndex:1];
                    [encoder setVertexBuffer:skinInputs offset:0 atIndex:11];
                    [encoder setVertexBuffer:jointPalette offset:0 atIndex:12];
                    for (NSUInteger i = 0; i < modelTextures.size(); ++i)
                        [encoder setFragmentTexture:modelTextures[i] atIndex:(i < 5 ? 2 + i : 28 + i - 5)];
                    [encoder setFragmentSamplerState:modelSampler atIndex:7];
                    [encoder setFragmentTexture:environmentTexture atIndex:8];
                    [encoder setFragmentTexture:backgroundTexture atIndex:9];
                    [encoder setFragmentTexture:gpuFrame ? (__bridge id<MTLTexture>)rasterAA.device->GPUOutput()
                                                : path   ? pathDisplay
                                                         : modelTextures[0]
                                        atIndex:10];
                    if ((environment && environmentSettings.showBackground) || path)
                    {
                        [encoder setRenderPipelineState:skyPipeline];
                        [encoder setDepthStencilState:skyDepthState];
                        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                    }
                    [encoder setRenderPipelineState:modelPipeline];
                    [encoder setDepthStencilState:depthState];
                    if (!path && scene.active)
                    {
                        for (const auto& draw : scene.draws)
                            if (draw.instanceCount)
                                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                            vertexStart:draw.firstVertex
                                            vertexCount:draw.vertexCount
                                          instanceCount:draw.instanceCount
                                           baseInstance:draw.firstInstance];
                    }
                    else if (!path && !model->vertices.empty())
                        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                    vertexStart:0
                                    vertexCount:model->vertices.size()];
                }
                else
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
#ifdef NOVA_HAS_UI
                if (panelEnabled)
                    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), command, encoder);
#endif
                [encoder endEncoding];
                [command presentDrawable:drawable];
                [command commit];
                lastCommand = command;
                return FrameResult::Rendered;
            }
        }
        void WaitIdle() override
        {
            if (!lastCommand)
                return;
            [lastCommand waitUntilCompleted];
            if (lastCommand.status == MTLCommandBufferStatusError)
            {
                ++errors;
                NSError* error = lastCommand.error;
                lastCommand    = nil;
                throw metalError("Execute Metal command buffer", error);
            }
            lastCommand = nil;
        }
        uint32_t ValidationErrorCount() const override { return errors; }

      private:
#ifdef NOVA_HAS_UI
        bool uiRenderer = false;
#endif
        RendererConfig                         settings;
        id<MTLBuffer>                          skinInputs, jointPalette;
        std::shared_ptr<const ModelData>       gpuModel;
        std::shared_ptr<const EnvironmentData> gpuEnvironment;
        id<MTLRenderPipelineState> aaModel[2], aaSky[2], aaTrianglePipeline[2], aaMotion, aaSkyMotion, aaGBuffer,
          aaDeferred, aaOit, aaHybridShadow, aaHybridReflection, aaHybridAO, aaProbeReflection, aaDeferredHalf;
        id<MTLComputePipelineState> probeUpdatePipeline, probeSharedUpdatePipeline;
        id<MTLRenderPipelineState>  probeDeferredPipeline, probeDeferredHalf;
        id<MTLBuffer>               probeCurrent, probePrevious;
        bool                        probeFrame = false;
        void                        EncodeProbeUpdate(id<MTLCommandBuffer> command)
        {
            bool reset;
            probeFrame = PrepareProbes(reset);
            if (!probeFrame)
                return;
            auto& pipeline = hybrid.probeSharedRays ? probeSharedUpdatePipeline : probeUpdatePipeline;
            if (!pipeline)
            {
                const auto bytes = LoadShader(settings.shaderDirectory,
                  hybrid.probeSharedRays ? "model-probe-update-shared.metallib" : "model-probe-update.metallib");
                auto       blob  = dispatch_data_create(
                  bytes.data(),
                  bytes.size(),
                  dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
                  DISPATCH_DATA_DESTRUCTOR_DEFAULT);
                NSError* error   = nil;
                auto     library = [device newLibraryWithData:blob error:&error];
                if (!library)
                    throw metalError("Load probe update shader", error);
                pipeline = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"computeMain"]
                                                                  error:&error];
                if (!pipeline)
                    throw metalError("Create probe update pipeline", error);
            }
            if (!probeCurrent)
            {
                probeCurrent  = [device newBufferWithLength:ProbeCount * ProbeBytes
                                                   options:MTLResourceStorageModePrivate];
                probePrevious = [device newBufferWithLength:ProbeCount * ProbeBytes
                                                    options:MTLResourceStorageModePrivate];
                reset         = true;
            }
            auto blit = [command blitCommandEncoder];
            if (reset)
                [blit fillBuffer:probeCurrent range:NSMakeRange(0, ProbeCount * ProbeBytes) value:0];
            const bool feedback = hybrid.probeSkyLighting && hybrid.probeMultiBounce && bool(environment);
            const auto offset   = feedback ? 0 : probeConfig.dimensionsFirst[3] * ProbeBytes;
            const auto bytes    = (feedback ? ProbeCount : probeConfig.count) * ProbeBytes;
            [blit copyFromBuffer:probeCurrent
                    sourceOffset:offset
                        toBuffer:probePrevious
               destinationOffset:offset
                            size:bytes];
            [blit endEncoding];
            auto encoder = [command computeCommandEncoder];
            [encoder setComputePipelineState:pipeline];
            [encoder setBytes:&rasterAA.current length:sizeof(SceneUniforms) atIndex:0];
            [encoder setBuffer:sceneInstanceBuffer offset:0 atIndex:14];
            [encoder setBytes:&probeConfig length:sizeof(probeConfig) atIndex:25];
            [encoder setBuffer:probePrevious offset:0 atIndex:26];
            [encoder setBuffer:probeCurrent offset:0 atIndex:27];
            for (NSUInteger i = 0; i < modelTextures.size(); ++i)
                [encoder setTexture:modelTextures[i] atIndex:(i < 5 ? 2 + i : 28 + i - 5)];
            [encoder setTexture:environmentTexture atIndex:8];
            [encoder setSamplerState:modelSampler atIndex:7];
            const auto groups = hybrid.probeSharedRays ? probeConfig.count : (probeConfig.count * 6 + 31) / 32;
            [encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
                    threadsPerThreadgroup:MTLSizeMake(hybrid.probeSharedRays ? 96 : 32, 1, 1)];
            [encoder endEncoding];
        }
        id<MTLDepthStencilState>   oitDepthState;
        id<MTLBuffer>              aaPreviousPalette;
        id<MTLRenderPipelineState> AAPipeline(const char* file, uint32_t samples)
        {
            const auto bytes = LoadShader(settings.shaderDirectory, file);
            NSData*    data  = [NSData dataWithBytes:bytes.data() length:bytes.size()];
            auto       blob =
              dispatch_data_create(data.bytes, data.length, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
                (void)data;
              });
            NSError* error   = nil;
            auto     library = [device newLibraryWithData:blob error:&error];
            if (!library)
                throw metalError("Load raster AA shader", error);
            auto desc                            = [MTLRenderPipelineDescriptor new];
            desc.vertexFunction                  = [library newFunctionWithName:@"vertexMain"];
            desc.fragmentFunction                = [library newFunctionWithName:@"fragmentMain"];
            desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
            if (std::string(file) == "model-gbuffer.metallib" || std::string(file) == "model-hybrid-shadow.metallib")
                for (int i = 1; i < 4; i++)
                    desc.colorAttachments[i].pixelFormat = MTLPixelFormatRGBA16Float;
            if (std::string(file) == "model-gbuffer.metallib")
                desc.colorAttachments[4].pixelFormat = MTLPixelFormatRGBA16Float;
            if (std::string(file) == "model-oit.metallib")
            {
                desc.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
                for (int i = 0; i < 2; i++)
                {
                    auto a                 = desc.colorAttachments[i];
                    a.blendingEnabled      = YES;
                    a.sourceRGBBlendFactor = a.sourceAlphaBlendFactor = i == 0 ? MTLBlendFactorOne : MTLBlendFactorZero;
                    a.destinationRGBBlendFactor                       = a.destinationAlphaBlendFactor =
                      i == 0 ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
                }
            }
            if (std::string_view(file) != "model-hybrid-reflection.metallib"
                && std::string_view(file) != "model-probe-reflection.metallib"
                && std::string_view(file) != "model-hybrid-ao.metallib")
                desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            desc.sampleCount                = samples;
            auto pipeline                   = [device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!pipeline)
                throw metalError("Create raster AA pipeline", error);
            return pipeline;
        }
        void CreateRasterAADevice() override
        {
            auto d                 = [MTLDepthStencilDescriptor new];
            d.depthCompareFunction = MTLCompareFunctionLessEqual;
            d.depthWriteEnabled    = NO;
            oitDepthState          = [device newDepthStencilStateWithDescriptor:d];
            AAExternalScene external;
            external.device      = (__bridge void*)device;
            external.bindGBuffer = [](void* encoder, const std::array<void*, 10>& views, int) {
                auto e = (__bridge id<MTLRenderCommandEncoder>)encoder;
                for (size_t i = 0; i < views.size(); i++)
                    [e setFragmentTexture:(__bridge id<MTLTexture>)views[i] atIndex:(i < 8 ? 17 + i : 31 + i - 8)];
            };
            external.beforeScene = [this](void* command) { EncodeProbeUpdate((__bridge id<MTLCommandBuffer>)command); };
            external.draw        = [this](const AAPass& pass, void* encoder, void*) {
                id<MTLRenderCommandEncoder> e      = (__bridge id<MTLRenderCommandEncoder>)encoder;
                const int                   sample = pass.multisampled ? 1 : 0;
                if (aaTriangle)
                {
                    if (!aaTrianglePipeline[sample])
                        aaTrianglePipeline[sample] = AAPipeline("triangle.metallib", sample ? 4 : 1);
                    [e setRenderPipelineState:aaTrianglePipeline[sample]];
                    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                    return;
                }
                const auto& current  = rasterAA.current;
                const auto& previous = rasterAA.frames ? rasterAA.previous : current;
                const auto& joints   = rasterAA.frames ? rasterAA.previousJoints : model->jointMatrices;
                size_t      bytes    = std::max(size_t(64), joints.size() * sizeof(JointMatrix));
                if (!aaPreviousPalette || aaPreviousPalette.length < bytes)
                    aaPreviousPalette = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
                if (!joints.empty())
                    std::memcpy(aaPreviousPalette.contents, joints.data(), joints.size() * sizeof(JointMatrix));
                [e setVertexBytes:&current length:sizeof(current) atIndex:0];
                [e setFragmentBytes:&current length:sizeof(current) atIndex:0];
                [e setVertexBytes:&previous length:sizeof(previous) atIndex:15];
                [e setFragmentBytes:&previous length:sizeof(previous) atIndex:15];
                [e setVertexBuffer:vertices offset:0 atIndex:1];
                [e setVertexBuffer:skinInputs offset:0 atIndex:11];
                [e setVertexBuffer:jointPalette offset:0 atIndex:12];
                [e setVertexBuffer:aaPreviousPalette offset:0 atIndex:16];
                [e setVertexBuffer:sceneInstanceBuffer offset:0 atIndex:14];
                [e setFragmentBuffer:sceneInstanceBuffer offset:0 atIndex:14];
                for (NSUInteger i = 0; i < modelTextures.size(); i++)
                    [e setFragmentTexture:modelTextures[i] atIndex:(i < 5 ? 2 + i : 28 + i - 5)];
                [e setFragmentTexture:environmentTexture atIndex:8];
                [e setFragmentTexture:backgroundTexture atIndex:9];
                [e setFragmentSamplerState:modelSampler atIndex:7];
                if (pass.program == -5 || pass.program == -7 || pass.program == -8 || pass.program == -9)
                {
                    const bool probeReflection = pass.program == -8 && probeFrame && environment
                                                 && hybrid.probeSkyLighting
                                                 && (hybrid.probeReflectionLighting || hybrid.probeRoughReflections);
                    const bool probes = (pass.program == -5 && probeFrame) || probeReflection;
                    const bool half = pass.program == -5 && pass.sources[9] != AADummy;
                    auto&      active = probeReflection      ? aaProbeReflection
                                        : pass.program == -9 ? aaHybridAO
                                   : pass.program == -8 ? aaHybridReflection
                                        : pass.program == -7 ? aaHybridShadow
                                        : probes && half     ? probeDeferredHalf
                                        : probes             ? probeDeferredPipeline
                                        : half               ? aaDeferredHalf
                                                             : aaDeferred;
                    if (!active)
                        active = AAPipeline(
                          probeReflection      ? "model-probe-reflection.metallib"
                          : pass.program == -9 ? "model-hybrid-ao.metallib"
                          : pass.program == -8 ? "model-hybrid-reflection.metallib"
                          : pass.program == -7 ? "model-hybrid-shadow.metallib"
                          : probes && half     ? "model-probe-deferred-half.metallib"
                          : probes             ? "model-probe-deferred.metallib"
                          : half               ? "model-deferred-half.metallib"
                                               : "model-deferred.metallib",
                          1);
                    if (probes)
                    {
                        [e setFragmentBytes:&probeConfig length:sizeof(probeConfig) atIndex:25];
                        [e setFragmentBuffer:probeCurrent offset:0 atIndex:26];
                    }
                    [e setRenderPipelineState:active];
                    [e setDepthStencilState:skyDepthState];
                    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                    return;
                }
                const bool oit     = pass.program == -6;
                const bool gbuffer = pass.program == -4;
                const bool motion  = pass.program == -2;
                if (!gbuffer && !oit && environment && environmentSettings.showBackground)
                {
                    if (motion)
                    {
                        if (!aaSkyMotion)
                            aaSkyMotion = AAPipeline("sky-motion.metallib", 1);
                        [e setRenderPipelineState:aaSkyMotion];
                    }
                    else
                    {
                        if (!aaSky[sample])
                            aaSky[sample] = AAPipeline("skybox.metallib", sample ? 4 : 1);
                        [e setRenderPipelineState:aaSky[sample]];
                    }
                    [e setDepthStencilState:skyDepthState];
                    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                }
                if (!model->vertices.empty())
                {
                    if (oit)
                    {
                        if (!aaOit)
                            aaOit = AAPipeline("model-oit.metallib", 1);
                        [e setRenderPipelineState:aaOit];
                    }
                    else if (gbuffer)
                    {
                        if (!aaGBuffer)
                            aaGBuffer = AAPipeline("model-gbuffer.metallib", 1);
                        [e setRenderPipelineState:aaGBuffer];
                    }
                    else if (motion)
                    {
                        if (!aaMotion)
                            aaMotion = AAPipeline("model-motion.metallib", 1);
                        [e setRenderPipelineState:aaMotion];
                    }
                    else
                    {
                        if (!aaModel[sample])
                            aaModel[sample] = AAPipeline("model.metallib", sample ? 4 : 1);
                        [e setRenderPipelineState:aaModel[sample]];
                    }
                    [e setDepthStencilState:oit ? oitDepthState : depthState];
                    if (scene.active)
                    {
                        for (const auto& draw : scene.draws)
                            if (draw.instanceCount)
                                [e drawPrimitives:MTLPrimitiveTypeTriangle
                                      vertexStart:draw.firstVertex
                                      vertexCount:draw.vertexCount
                                    instanceCount:draw.instanceCount
                                     baseInstance:draw.firstInstance];
                    }
                    else
                        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:model->vertices.size()];
                }
            };
            rasterAA.device = CreateMetalAA(settings, external);
        }
        id<MTLTexture>                environmentTexture, backgroundTexture, pathDisplay;
        id<MTLRenderPipelineState>    skyPipeline;
        id<MTLDepthStencilState>      skyDepthState;
        id<MTLBuffer>                 vertices;
        id<MTLBuffer>                 sceneInstanceBuffer;
        id<MTLRenderPipelineState>    modelPipeline;
        id<MTLDepthStencilState>      depthState;
        id<MTLTexture>                depth;
        std::array<id<MTLTexture>, 8> modelTextures{};
        id<MTLSamplerState>           modelSampler;
        void                          UploadPathImage(const ModelImage& image)
        {
            if (!pathDisplay || pathDisplay.width != image.width || pathDisplay.height != image.height)
            {
                auto desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                                                               width:image.width
                                                                              height:image.height
                                                                           mipmapped:NO];
                desc.storageMode = MTLStorageModeShared;
                desc.usage       = MTLTextureUsageShaderRead;
                pathDisplay      = [device newTextureWithDescriptor:desc];
                if (!pathDisplay)
                    throw std::runtime_error("Metal path display allocation failed");
            }
            [pathDisplay replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height)
                           mipmapLevel:0
                             withBytes:image.pixels.data()
                           bytesPerRow:image.width * 4];
        }
        void UploadModel()
        {
            const auto      shader = LoadShader(settings.shaderDirectory, "model.metallib");
            NSData*         data   = [NSData dataWithBytes:shader.data() length:shader.size()];
            dispatch_data_t blob =
              dispatch_data_create(data.bytes, data.length, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
                (void)data;
              });
            NSError*       error   = nil;
            id<MTLLibrary> library = [device newLibraryWithData:blob error:&error];
            if (!library)
                throw metalError("Load model.metallib", error);
            MTLRenderPipelineDescriptor* desc    = [MTLRenderPipelineDescriptor new];
            desc.vertexFunction                  = [library newFunctionWithName:@"vertexMain"];
            desc.fragmentFunction                = [library newFunctionWithName:@"fragmentMain"];
            desc.colorAttachments[0].pixelFormat = layer.pixelFormat;
            desc.depthAttachmentPixelFormat      = MTLPixelFormatDepth32Float;
            modelPipeline                        = [device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!modelPipeline)
                throw metalError("Create model pipeline", error);
            if (model->vertices.empty())
                vertices = [device newBufferWithLength:sizeof(ModelVertex) options:MTLResourceStorageModeShared];
            else
                vertices = [device newBufferWithBytes:model->vertices.data()
                                               length:model->vertices.size() * sizeof(ModelVertex)
                                              options:MTLResourceStorageModeShared];
            if (!vertices)
                throw std::runtime_error("Create model vertex buffer failed");
            skinInputs = [device newBufferWithLength:std::max(size_t(64), model->skinInputs.size() * sizeof(SkinInput))
                                             options:MTLResourceStorageModeShared];
            jointPalette =
              [device newBufferWithLength:std::max(size_t(64), model->jointMatrices.size() * sizeof(JointMatrix))
                                  options:MTLResourceStorageModeShared];
            if (!skinInputs || !jointPalette)
                throw std::runtime_error("Create GPU skin buffers failed");
            if (!model->skinInputs.empty())
                std::memcpy(
                  skinInputs.contents, model->skinInputs.data(), model->skinInputs.size() * sizeof(SkinInput));
            if (!model->jointMatrices.empty())
                std::memcpy(
                  jointPalette.contents,
                  model->jointMatrices.data(),
                  model->jointMatrices.size() * sizeof(JointMatrix));
            for (size_t i = 0; i < modelTextures.size(); ++i)
            {
                const auto  levels  = MaterialMipmaps(model->images[i], i);
                const auto& image   = levels[0];
                auto        texture = [MTLTextureDescriptor
                  texture2DDescriptorWithPixelFormat:(i == 0 || i == 4) ? MTLPixelFormatRGBA8Unorm_sRGB
                                                                               : MTLPixelFormatRGBA8Unorm
                                               width:image.width
                                              height:image.height
                                           mipmapped:levels.size() > 1];
                texture.usage       = MTLTextureUsageShaderRead;
                texture.storageMode = MTLStorageModeShared;
                modelTextures[i]    = [device newTextureWithDescriptor:texture];
                if (!modelTextures[i])
                    throw std::runtime_error("Create material mip texture failed");
                for (size_t level = 0; level < levels.size(); level++)
                {
                    const auto& mip = levels[level];
                    [modelTextures[i] replaceRegion:MTLRegionMake2D(0, 0, mip.width, mip.height)
                                        mipmapLevel:level
                                          withBytes:mip.pixels.data()
                                        bytesPerRow:mip.width * 4];
                }
            }
            const auto& environmentData = environment ? *environment : FallbackEnvironment();
            auto        uploadCube      = [&](const std::vector<EnvironmentMip>& mips) -> id<MTLTexture> {
                MTLTextureDescriptor* cube =
                  [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                        size:mips[0].size
                                                                   mipmapped:mips.size() > 1];
                cube.storageMode       = MTLStorageModeShared;
                cube.usage             = MTLTextureUsageShaderRead;
                id<MTLTexture> texture = [device newTextureWithDescriptor:cube];
                if (!texture)
                    throw std::runtime_error("Create HDR environment cube failed");
                for (size_t level = 0; level < mips.size(); ++level)
                {
                    const auto& mip = mips[level];
                    for (NSUInteger face = 0; face < 6; ++face)
                        [texture replaceRegion:MTLRegionMake2D(0, 0, mip.size, mip.size)
                                   mipmapLevel:level
                                         slice:face
                                     withBytes:mip.pixels.data() + size_t(face) * mip.size * mip.size * 4
                                   bytesPerRow:mip.size * 8
                                 bytesPerImage:size_t(mip.size) * mip.size * 8];
                }
                return texture;
            };
            environmentTexture = uploadCube(environmentData.mips);
            backgroundTexture  = uploadCube(BackgroundMips(environmentData));
            if (model)
            {
                const auto      bytes   = LoadShader(settings.shaderDirectory, "skybox.metallib");
                NSData*         skyData = [NSData dataWithBytes:bytes.data() length:bytes.size()];
                dispatch_data_t skyBlob = dispatch_data_create(
                  skyData.bytes, skyData.length, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
                    (void)skyData;
                  });
                id<MTLLibrary> skyLibrary = [device newLibraryWithData:skyBlob error:&error];
                if (!skyLibrary)
                    throw metalError("Load skybox.metallib", error);
                desc.vertexFunction   = [skyLibrary newFunctionWithName:@"vertexMain"];
                desc.fragmentFunction = [skyLibrary newFunctionWithName:@"fragmentMain"];
                skyPipeline           = [device newRenderPipelineStateWithDescriptor:desc error:&error];
                if (!skyPipeline)
                    throw metalError("Create skybox pipeline", error);
            }
            MTLDepthStencilDescriptor* skyDepth = [MTLDepthStencilDescriptor new];
            skyDepth.depthCompareFunction       = MTLCompareFunctionAlways;
            skyDepth.depthWriteEnabled          = NO;
            skyDepthState                       = [device newDepthStencilStateWithDescriptor:skyDepth];
            if (!skyDepthState)
                throw std::runtime_error("Create fullscreen depth state failed");
            MTLSamplerDescriptor* sampler        = [MTLSamplerDescriptor new];
            sampler.minFilter                    = MTLSamplerMinMagFilterLinear;
            sampler.magFilter                    = MTLSamplerMinMagFilterLinear;
            sampler.mipFilter                    = MTLSamplerMipFilterLinear;
            sampler.maxAnisotropy                = 8;
            sampler.lodMaxClamp                  = 32;
            sampler.sAddressMode                 = MTLSamplerAddressModeRepeat;
            sampler.tAddressMode                 = MTLSamplerAddressModeRepeat;
            modelSampler                         = [device newSamplerStateWithDescriptor:sampler];
            MTLDepthStencilDescriptor* depthDesc = [MTLDepthStencilDescriptor new];
            depthDesc.depthCompareFunction       = MTLCompareFunctionLess;
            depthDesc.depthWriteEnabled          = YES;
            depthState                           = [device newDepthStencilStateWithDescriptor:depthDesc];
            if (!modelSampler || !depthState)
                throw std::runtime_error("Create model sampler/depth state failed");
            gpuModel       = model;
            gpuEnvironment = environment;
        }
        id<MTLDevice>              device;
        id<MTLCommandQueue>        queue;
        id<MTLRenderPipelineState> pipeline;
        id<MTLCommandBuffer>       lastCommand;
        SDL_MetalView              metalView = nullptr;
        CAMetalLayer*              layer = nil;
        uint32_t                   width = 0, height = 0, errors = 0;
        bool                       resized = true;
    };
    std::unique_ptr<Backend> CreateMetalBackend(PresentationWindow window, const RendererConfig& config)
    {
        auto backend = std::make_unique<MetalBackend>();
        backend->Initialize(window, config);
        return backend;
    }
} // namespace render::detail
