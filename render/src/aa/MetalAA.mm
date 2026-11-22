#include "AADevice.hpp"
#include "ShaderData.hpp"
#import <Metal/Metal.h>
#include <stdexcept>
namespace render::detail
{
    namespace
    {
        class MetalAA final : public AADevice
        {
            AAExternalScene             external;
            id<MTLDevice>               device;
            id<MTLCommandQueue>         queue;
            id<MTLTexture>              textures[AAResourceCount], depth, depthMS, depthBaseline, depthShadow;
            id<MTLRenderPipelineState>  pipelines[31], sceneMS, shadowMS, hdrMS, ssrMS;
            id<MTLComputePipelineState> compute[2];
            id<MTLSamplerState>         linear, point;
            id<MTLDepthStencilState>    depthState;
            uint32_t                    width = 0, height = 0;
            bool                        super = false;
            id<MTLTexture>              Texture(uint32_t w, uint32_t h, MTLPixelFormat format, NSUInteger samples = 1)
            {
                auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
                d.storageMode = MTLStorageModePrivate;
                d.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
                if (format == MTLPixelFormatRGBA32Float)
                    d.usage |= MTLTextureUsageShaderWrite;
                if (samples > 1)
                {
                    d.textureType = MTLTextureType2DMultisample;
                    d.sampleCount = samples;
                }
                auto t = [device newTextureWithDescriptor:d];
                if (!t)
                    throw std::runtime_error("Metal AA texture allocation failed");
                return t;
            }
            id<MTLRenderPipelineState> Pipeline(id<MTLLibrary> library, int program, NSUInteger samples)
            {
                auto d                            = [MTLRenderPipelineDescriptor new];
                d.vertexFunction                  = [library newFunctionWithName:@"VSMain"];
                d.fragmentFunction                = [library newFunctionWithName:@"PSMain"];
                d.sampleCount                     = samples;
                d.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
                if (
                  program == 0 || program == 10 || program == 16 || program == 17 || program == 22 || program == 23
                  || program == 27)
                {
                    if (program != 23 && program != 27)
                        d.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
                    if (program == 17 || program == 22)
                    {
                        d.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA16Float;
                        d.colorAttachments[3].pixelFormat = MTLPixelFormatRGBA16Float;
                    }
                    d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
                }
                if (program == 9)
                    d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
                NSError* error = nil;
                auto     p     = [device newRenderPipelineStateWithDescriptor:d error:&error];
                if (!p)
                    throw std::runtime_error(error.localizedDescription.UTF8String);
                return p;
            }

          public:
            explicit MetalAA(const RendererConfig& config, const AAExternalScene& borrowed)
              : external(borrowed)
            {
                device = external.device ? (__bridge id<MTLDevice>)external.device : MTLCreateSystemDefaultDevice();
                if (!device)
                    throw std::runtime_error("Metal AA device unavailable");
                if (![device supportsTextureSampleCount:4])
                    throw std::runtime_error("Metal AA requires 4x MSAA support");
                queue = [device newCommandQueue];
                for (int i = 0; i < 31; i++)
                {
                    const auto data =
                      LoadShader(config.shaderDirectory, ("aa-" + std::to_string(i) + ".metallib").c_str());
                    NSData* storage = [NSData dataWithBytes:data.data() length:data.size()];
                    auto    blob    = dispatch_data_create(
                      storage.bytes, storage.length, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
                        (void)storage;
                      });
                    NSError* error = nil;
                    auto     lib   = [device newLibraryWithData:blob error:&error];
                    if (!lib)
                        throw std::runtime_error(error.localizedDescription.UTF8String);
                    if (i == 25 || i == 26)
                    {
                        compute[i - 25] =
                          [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"PipelineCSMain"]
                                                                error:&error];
                        if (!compute[i - 25])
                            throw std::runtime_error(error.localizedDescription.UTF8String);
                        continue;
                    }
                    pipelines[i] = Pipeline(lib, i, 1);
                    if (i == 0)
                        sceneMS = Pipeline(lib, 0, 4);
                    if (i == 17)
                        ssrMS = Pipeline(lib, 17, 4);
                    if (i == 16)
                        hdrMS = Pipeline(lib, 16, 4);
                    if (i == 10)
                        shadowMS = Pipeline(lib, 10, 4);
                }
                auto sampler      = [MTLSamplerDescriptor new];
                sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterLinear;
                sampler.sAddressMode = sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
                linear                                      = [device newSamplerStateWithDescriptor:sampler];
                sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterNearest;
                point                                 = [device newSamplerStateWithDescriptor:sampler];
                auto ds                               = [MTLDepthStencilDescriptor new];
                ds.depthCompareFunction               = MTLCompareFunctionLess;
                ds.depthWriteEnabled                  = YES;
                depthState                            = [device newDepthStencilStateWithDescriptor:ds];
                for (int i = AAArea; i <= AASearch; i++)
                {
                    const auto data = AALookup(i == AAArea);
                    auto       d    = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                width:data.width
                                                                               height:data.height
                                                                            mipmapped:NO];
                    d.storageMode   = MTLStorageModeShared;
                    d.usage         = MTLTextureUsageShaderRead;
                    textures[i]     = [device newTextureWithDescriptor:d];
                    [textures[i] replaceRegion:MTLRegionMake2D(0, 0, data.width, data.height)
                                   mipmapLevel:0
                                     withBytes:data.pixels.data()
                                   bytesPerRow:data.width * 4];
                }
            }
            void SetReflectionEnvironment(const AATextureData& data, int slot) override
            {
                auto d        = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                            width:data.width
                                                                           height:data.height
                                                                        mipmapped:NO];
                d.storageMode = MTLStorageModeShared;
                d.usage       = MTLTextureUsageShaderRead;
                auto texture  = [device newTextureWithDescriptor:d];
                if (!texture)
                    throw std::runtime_error("SSR environment allocation failed");
                [texture replaceRegion:MTLRegionMake2D(0, 0, data.width, data.height)
                           mipmapLevel:0
                             withBytes:data.pixels.data()
                           bytesPerRow:data.width * 8];
                textures[slot] = texture;
            }
            void Resize(uint32_t w, uint32_t h, bool ss) override
            {
                width  = w;
                height = h;
                super  = ss;
            }
            void Ensure(const std::vector<AAPass>& passes)
            {
                const auto used = AAUsed(passes);
                for (int i = 0; i < AAResourceCount; i++)
                    if (i != AAArea && i != AASearch && i != AASsrEnvironment && i != AAProbeData)
                    {
                        if (!used[i])
                        {
                            textures[i] = nil;
                            continue;
                        }
                        const auto spec = AAResourceSpec(
                          i,
                          width,
                          height,
                          super,
                          bool(external.draw),
                          uint32_t(passes.front().uniforms[8][1]),
                          passes.front().uniforms[6][3] > .5f);
                        if (!textures[i] || textures[i].width != spec.width || textures[i].height != spec.height)
                            textures[i] = Texture(
                              spec.width,
                              spec.height,
                              (i == AAPipelineTiles || i == AAPipelineLists) ? MTLPixelFormatRGBA32Float
                                                                             : MTLPixelFormatRGBA16Float,
                              spec.samples);
                    }
                bool needed[4]{};
                for (const auto& p : passes)
                    if (((p.program <= 0 && p.program != -8 && p.program != -9) || p.program == 9 || p.program == 10 || p.program == 16 || p.program == 17
                         || p.program == 22 || p.program == 23 || p.program == 27))
                    {
                        const int k       = p.program == 9                                          ? 3
                                            : (p.target == AABaseline || p.target == AARawBaseline) ? 2
                                            : p.multisampled                                        ? 1
                                                                                                    : 0;
                        needed[k]         = true;
                        auto&      d      = k == 3 ? depthShadow : k == 2 ? depthBaseline : k == 1 ? depthMS : depth;
                        const auto target = textures[p.target];
                        if (!d || d.width != target.width || d.height != target.height)
                            d = Texture(
                              uint32_t(target.width),
                              uint32_t(target.height),
                              MTLPixelFormatDepth32Float,
                              k == 1 ? 4 : 1);
                    }
                if (!needed[3])
                    depthShadow = nil;
                if (!needed[0])
                    depth = nil;
                if (!needed[1])
                    depthMS = nil;
                if (!needed[2])
                    depthBaseline = nil;
            }
            void* GPUOutput() const override { return (__bridge void*)textures[AAOutput]; }

            Image Capture() override
            {
                if (!textures[AAOutput])
                    throw std::runtime_error("Draw before GPU capture");
                const size_t row     = (size_t(width) * 8 + 255) & ~size_t(255);
                auto         buffer  = [device newBufferWithLength:row * height options:MTLResourceStorageModeShared];
                auto         command = [queue commandBuffer];
                auto         blit    = [command blitCommandEncoder];
                [blit copyFromTexture:textures[AAOutput]
                               sourceSlice:0
                               sourceLevel:0
                              sourceOrigin:MTLOriginMake(0, 0, 0)
                                sourceSize:MTLSizeMake(width, height, 1)
                                  toBuffer:buffer
                         destinationOffset:0
                    destinationBytesPerRow:row
                  destinationBytesPerImage:row * height];
                [blit endEncoding];
                [command commit];
                [command waitUntilCompleted];
                if (command.status != MTLCommandBufferStatusCompleted)
                    throw std::runtime_error(command.error.localizedDescription.UTF8String);
                readbackCount++;
                return AAReadImage(static_cast<const uint16_t*>(buffer.contents), width, height, row);
            }
            Image Draw(const std::vector<AAPass>& passes, bool readbackImage = true) override
            {
                @autoreleasepool
                {
                    Ensure(passes);
                    auto command = [queue commandBuffer];
                    if (external.beforeScene)
                        external.beforeScene((__bridge void*)command);
                    // Clear history/dummy inputs before sampling, even on the first invalid-history frame.
                    for (int i : {AAHistoryA, AAHistoryB, AAPrevMotion, AADummy})
                        if (textures[i] && (passes.front().uniforms[3][0] < .5f || i == AADummy))
                        {
                            auto d                            = [MTLRenderPassDescriptor renderPassDescriptor];
                            d.colorAttachments[0].texture     = textures[i];
                            d.colorAttachments[0].loadAction  = MTLLoadActionClear;
                            d.colorAttachments[0].storeAction = MTLStoreActionStore;
                            auto e                            = [command renderCommandEncoderWithDescriptor:d];
                            [e endEncoding];
                        }
                    for (const auto& p : passes)
                    {
                        if (p.program == 25 || p.program == 26)
                        {
                            auto e = [command computeCommandEncoder];
                            [e setComputePipelineState:compute[p.program - 25]];
                            [e setBytes:p.uniforms.data() length:sizeof(AAUniforms) atIndex:0];
                            [e setTexture:textures[p.sources[4]] atIndex:5];
                            [e setTexture:textures[p.sources[5]] atIndex:6];
                            [e setTexture:textures[p.target] atIndex:19];
                            [e dispatchThreadgroups:MTLSizeMake(
                                                      16, 9, p.program == 25 && p.uniforms[0][2] > 2.5f ? 16 : 1)
                              threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                            [e endEncoding];
                            continue;
                        }
                        auto       d = [MTLRenderPassDescriptor renderPassDescriptor];
                        const bool scene =
                                     ((p.program <= 0 && p.program != -8 && p.program != -9) || p.program == 9 || p.program == 10 || p.program == 16
                                      || p.program == 17 || p.program == 22 || p.program == 23 || p.program == 27),
                                   nativeScene = p.program < 0,
                                   baseline    = p.target == AABaseline || p.target == AARawBaseline;
                        const int attachments =
                          p.program == -4                                                             ? 7
                          : (p.program == 17 || p.program == 22 || p.program == -7)                   ? 4
                          : (p.program == 0 || p.program == 10 || p.program == 16 || p.program == -6) ? 2
                                                                                                      : 1;
                        for (int a = 0; a < attachments; a++)
                        {
                            auto      c      = d.colorAttachments[a];
                            const int target = p.program == -4 && a == 6 ? AAModelFilmMap
                                      : p.program == -4 && a == 5 ? AAModelSpecularMap
                                      : p.program == -4 && a == 4 ? AAModelClearcoat : p.target + a;
                            c.texture        = textures
                              [p.multisampled ? (p.program == 17 ? AASsrMulti0
                                                 : nativeScene   ? AARawMultiColor
                                                                 : AAMultiColor)
                                                  + a
                                              : target];
                            c.loadAction = MTLLoadActionClear;
                            c.clearColor =
                              (a || p.program == -2 || p.program == -3)
                                ? MTLClearColorMake(0, 0, 1, 1)
                                : MTLClearColorMake(
                                    p.uniforms[7][0], p.uniforms[7][1], p.uniforms[7][2], p.uniforms[7][3]);
                            if (p.program == 17 && a == 2)
                                c.clearColor = MTLClearColorMake(0, 0, 0, 1);
                            if (p.program == 17 && a == 3)
                                c.clearColor = MTLClearColorMake(1000, 1000, 0, 1);
                            if ((p.program >= 22 && p.program != 23) || p.program == -4 || p.program == -7)
                                c.clearColor = MTLClearColorMake(0, 0, 0, 0);
                            if (p.program == -6)
                                c.clearColor = a == 0 ? MTLClearColorMake(0, 0, 0, 0) : MTLClearColorMake(1, 1, 1, 1);
                            c.storeAction = p.multisampled ? MTLStoreActionMultisampleResolve : MTLStoreActionStore;
                            if (p.multisampled)
                                c.resolveTexture = textures[target];
                        }
                        if (scene)
                        {
                            d.depthAttachment.texture     = p.program == 9   ? depthShadow
                                                            : baseline       ? depthBaseline
                                                            : p.multisampled ? depthMS
                                                                             : depth;
                            d.depthAttachment.loadAction  = (p.program == -5 || p.program == -6 || p.program == -7)
                                                              ? MTLLoadActionLoad
                                                              : MTLLoadActionClear;
                            d.depthAttachment.clearDepth  = 1;
                            d.depthAttachment.storeAction = MTLStoreActionStore;
                        }
                        auto       e  = [command renderCommandEncoderWithDescriptor:d];
                        const auto rt = textures[p.target];
                        if (nativeScene)
                        {
                            [e setViewport:MTLViewport{
                                             1 + p.uniforms[1][0],
                                             1 + p.uniforms[1][1],
                                             double(rt.width - 2),
                                             double(rt.height - 2),
                                             0,
                                             1}];
                            [e setCullMode:MTLCullModeNone];
                            [e setFrontFacingWinding:MTLWindingCounterClockwise];
                            [e setDepthStencilState:depthState];
                            if ((p.program == -5 || p.program == -6 || p.program == -7 || p.program == -8 || p.program == -9) && external.bindGBuffer)
                            {
                                std::array<void*, 12> views{};
                                for (size_t i = 0; i < views.size(); i++)
                                    views[i] = (__bridge void*)textures[p.sources[i]];
                                external.bindGBuffer((__bridge void*)e, views, p.program);
                            }
                            if (p.program != -3)
                                external.draw(p, (__bridge void*)e, nullptr);
                            [e endEncoding];
                            continue;
                        }
                        [e setRenderPipelineState:p.multisampled ? (p.program == 17   ? ssrMS
                                                                    : p.program == 16 ? hdrMS
                                                                    : p.program == 10 ? shadowMS
                                                                                      : sceneMS)
                                                                 : pipelines[p.program]];
                        const auto target = textures[p.target];
                        [e setViewport:MTLViewport{0, 0, double(target.width), double(target.height), 0, 1}];
                        [e setCullMode:MTLCullModeNone];
                        [e setFrontFacingWinding:MTLWindingCounterClockwise];
                        [e setVertexBytes:p.uniforms.data() length:sizeof(AAUniforms) atIndex:0];
                        [e setFragmentBytes:p.uniforms.data() length:sizeof(AAUniforms) atIndex:0];
                        if (scene)
                            [e setDepthStencilState:depthState];
                        for (int b = 0; b < 16; b++)
                            [e setFragmentTexture:textures[p.sources[b]] atIndex:b < 8 ? b + 1 : b + 3];
                        [e setVertexTexture:textures[p.sources[10]] atIndex:13];
                        [e setFragmentSamplerState:linear atIndex:9];
                        [e setFragmentSamplerState:point atIndex:10];
                        [e drawPrimitives:MTLPrimitiveTypeTriangle
                              vertexStart:0
                              vertexCount:(p.program == 9 || p.program == 10 || p.program == 16 || p.program == 17
                                           || p.program == 22 || p.program == 23 || p.program == 27)
                                            ? uint32_t(p.uniforms[10][2])
                                          : scene ? 79 * 6
                                                  : 3];
                        [e endEncoding];
                    }
                    if (!readbackImage)
                    {
                        [command commit];
                        [command waitUntilCompleted];
                        if (command.status != MTLCommandBufferStatusCompleted)
                            throw std::runtime_error(command.error.localizedDescription.UTF8String);
                        return {width, height, {}};
                    }
                    readbackCount++;
                    const size_t row = (size_t(width) * 8 + 255) & ~size_t(255);
                    auto buffer      = [device newBufferWithLength:row * height options:MTLResourceStorageModeShared];
                    auto blit        = [command blitCommandEncoder];
                    [blit copyFromTexture:textures[AAOutput]
                                   sourceSlice:0
                                   sourceLevel:0
                                  sourceOrigin:MTLOriginMake(0, 0, 0)
                                    sourceSize:MTLSizeMake(width, height, 1)
                                      toBuffer:buffer
                             destinationOffset:0
                        destinationBytesPerRow:row
                      destinationBytesPerImage:row * height];
                    [blit endEncoding];
                    [command commit];
                    [command waitUntilCompleted];
                    if (command.status != MTLCommandBufferStatusCompleted)
                        throw std::runtime_error(command.error.localizedDescription.UTF8String);
                    return AAReadImage(static_cast<const uint16_t*>(buffer.contents), width, height, row);
                }
            }
        };
    } // namespace
    std::unique_ptr<AADevice> CreateMetalAA(const RendererConfig& c, const AAExternalScene& external)
    {
        return std::make_unique<MetalAA>(c, external);
    }
} // namespace render::detail
