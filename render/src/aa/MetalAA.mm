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
            id<MTLDevice>              device;
            id<MTLCommandQueue>        queue;
            id<MTLTexture>             textures[AAResourceCount], depth, depthMS, depthBaseline;
            id<MTLRenderPipelineState> pipelines[9], sceneMS;
            id<MTLSamplerState>        linear, point;
            id<MTLDepthStencilState>   depthState;
            uint32_t                   width = 0, height = 0;
            bool                       super = false;
            id<MTLTexture>             Texture(uint32_t w, uint32_t h, MTLPixelFormat format, NSUInteger samples = 1)
            {
                auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
                d.storageMode = MTLStorageModePrivate;
                d.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
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
                if (program == 0)
                {
                    d.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
                    d.depthAttachmentPixelFormat      = MTLPixelFormatDepth32Float;
                }
                NSError* error = nil;
                auto     p     = [device newRenderPipelineStateWithDescriptor:d error:&error];
                if (!p)
                    throw std::runtime_error(error.localizedDescription.UTF8String);
                return p;
            }

          public:
            explicit MetalAA(const RendererConfig& config)
            {
                device = MTLCreateSystemDefaultDevice();
                if (!device)
                    throw std::runtime_error("Metal AA device unavailable");
                if (![device supportsTextureSampleCount:4])
                    throw std::runtime_error("Metal AA requires 4x MSAA support");
                queue = [device newCommandQueue];
                for (int i = 0; i < 9; i++)
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
                    pipelines[i] = Pipeline(lib, i, 1);
                    if (i == 0)
                        sceneMS = Pipeline(lib, 0, 4);
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
            void Resize(uint32_t w, uint32_t h, bool ss) override
            {
                if (width == w && height == h && super == ss)
                    return;
                width  = w;
                height = h;
                super  = ss;
                for (int i = 0; i < AAResourceCount; i++)
                    if (i != AAArea && i != AASearch)
                    {
                        const bool     big = i == AAColor || i == AAMotion;
                        const uint32_t tw  = i == AADummy ? 1 : w * (big && ss ? 2 : 1),
                                       th  = i == AADummy ? 1 : h * (big && ss ? 2 : 1);
                        textures[i] =
                          Texture(tw, th, MTLPixelFormatRGBA16Float, (i == AAMultiColor || i == AAMultiMotion) ? 4 : 1);
                    }
                depth         = Texture(w * (ss ? 2 : 1), h * (ss ? 2 : 1), MTLPixelFormatDepth32Float);
                depthMS       = Texture(w, h, MTLPixelFormatDepth32Float, 4);
                depthBaseline = Texture(w, h, MTLPixelFormatDepth32Float);
            }
            Image Draw(const std::vector<AAPass>& passes) override
            {
                @autoreleasepool
                {
                    auto command = [queue commandBuffer];
                    // Clear history/dummy inputs before sampling, even on the first invalid-history frame.
                    for (int i : {AAHistoryA, AAHistoryB, AAPrevMotion, AADummy})
                        if (passes.front().uniforms[3][0] < .5f || i == AADummy)
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
                        auto       d     = [MTLRenderPassDescriptor renderPassDescriptor];
                        const bool scene = p.program == 0, baseline = p.target == AABaseline;
                        for (int a = 0; a < (scene ? 2 : 1); a++)
                        {
                            auto      c      = d.colorAttachments[a];
                            const int target = p.target + (a ? 1 : 0);
                            c.texture        = textures[p.multisampled ? AAMultiColor + a : target];
                            c.loadAction     = MTLLoadActionClear;
                            c.clearColor  = a ? MTLClearColorMake(0, 0, 1, 1) : MTLClearColorMake(.012, .02, .035, .1);
                            c.storeAction = p.multisampled ? MTLStoreActionMultisampleResolve : MTLStoreActionStore;
                            if (p.multisampled)
                                c.resolveTexture = textures[target];
                        }
                        if (scene)
                        {
                            d.depthAttachment.texture     = baseline ? depthBaseline : p.multisampled ? depthMS : depth;
                            d.depthAttachment.loadAction  = MTLLoadActionClear;
                            d.depthAttachment.clearDepth  = 1;
                            d.depthAttachment.storeAction = MTLStoreActionDontCare;
                        }
                        auto e = [command renderCommandEncoderWithDescriptor:d];
                        [e setRenderPipelineState:p.multisampled ? sceneMS : pipelines[p.program]];
                        const auto target = textures[p.target];
                        [e setViewport:MTLViewport{0, 0, double(target.width), double(target.height), 0, 1}];
                        [e setCullMode:MTLCullModeNone];
                        [e setVertexBytes:p.uniforms.data() length:sizeof(AAUniforms) atIndex:0];
                        [e setFragmentBytes:p.uniforms.data() length:sizeof(AAUniforms) atIndex:0];
                        if (scene)
                            [e setDepthStencilState:depthState];
                        for (int b = 0; b < 8; b++)
                            [e setFragmentTexture:textures[p.sources[b]] atIndex:b + 1];
                        [e setFragmentSamplerState:linear atIndex:9];
                        [e setFragmentSamplerState:point atIndex:10];
                        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:scene ? 79 * 6 : 3];
                        [e endEncoding];
                    }
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
    std::unique_ptr<AADevice> CreateMetalAA(const RendererConfig& c)
    {
        return std::make_unique<MetalAA>(c);
    }
} // namespace render::detail
