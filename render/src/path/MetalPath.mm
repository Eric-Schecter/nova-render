#include "PathTracing.hpp"
#include "ShaderData.hpp"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstring>
#include <stdexcept>
namespace render::detail
{
    namespace
    {
        void Finish(id<MTLCommandBuffer> command)
        {
            [command commit];
            [command waitUntilCompleted];
            if (command.status != MTLCommandBufferStatusCompleted)
                throw std::runtime_error(
                  command.error ? command.error.localizedDescription.UTF8String : "Metal path command failed");
        }
        class MetalPath final : public PathDevice
        {
            id<MTLDevice>                device;
            id<MTLCommandQueue>          queue;
            id<MTLComputePipelineState>  pipeline, denoisePipeline, photonClearPipeline, photonPipeline;
            id<MTLComputePipelineState>  mltPipelines[3], aoPipelines[4];
            id<MTLAccelerationStructure> blas, tlas;
            id<MTLBuffer>                vertices, texels, sky, params, accumulation, output, guides;
            uint32_t                     width = 0, height = 0;
            size_t                       scratchBytes = 0;
            id<MTLBuffer>                Buffer(const void* data, size_t size)
            {
                id<MTLBuffer> result = [device newBufferWithBytes:data
                                                           length:size
                                                          options:MTLResourceStorageModeShared];
                if (!result)
                    throw std::runtime_error("Metal path buffer allocation failed");
                return result;
            }
            id<MTLAccelerationStructure> Build(MTLAccelerationStructureDescriptor* descriptor)
            {
                const auto                   sizes = [device accelerationStructureSizesWithDescriptor:descriptor];
                id<MTLAccelerationStructure> result =
                  [device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
                id<MTLBuffer> scratch = [device newBufferWithLength:sizes.buildScratchBufferSize
                                                            options:MTLResourceStorageModePrivate];
                if (!result || !scratch)
                    throw std::runtime_error("Metal path AS allocation failed");
                auto command = [queue commandBuffer];
                auto encoder = [command accelerationStructureCommandEncoder];
                [encoder buildAccelerationStructure:result
                                         descriptor:descriptor
                                      scratchBuffer:scratch
                                scratchBufferOffset:0];
                [encoder endEncoding];
                Finish(command);
                return result;
            }

            id<MTLComputePipelineState> Pipeline(const RendererConfig& config, const char* file, NSString* entry)
            {
                const auto      bytes = LoadShader(config.shaderDirectory, file);
                NSData*         data  = [NSData dataWithBytes:bytes.data() length:bytes.size()];
                dispatch_data_t blob =
                  dispatch_data_create(data.bytes, data.length, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
                    (void)data;
                  });
                NSError* error   = nil;
                auto     library = [device newLibraryWithData:blob error:&error];
                if (!library)
                    throw std::runtime_error(error.localizedDescription.UTF8String);
                auto result = [device newComputePipelineStateWithFunction:[library newFunctionWithName:entry]
                                                                    error:&error];
                if (!result)
                    throw std::runtime_error(error.localizedDescription.UTF8String);
                return result;
            }

          public:
            explicit MetalPath(const RendererConfig& config)
            {
                device = MTLCreateSystemDefaultDevice();
                if (!device || !device.supportsRaytracing)
                    throw std::runtime_error("Metal device does not support ray queries");
                capabilities.pathTracing      = true;
                capabilities.rayTracingDevice = device.name.UTF8String;
                queue                         = [device newCommandQueue];
                params = [device newBufferWithLength:sizeof(PathUniforms) options:MTLResourceStorageModeShared];
                if (!params)
                    throw std::runtime_error("Metal path parameters allocation failed");
                pipeline            = Pipeline(config, "pathtrace.metallib", @"CSMain");
                denoisePipeline     = Pipeline(config, "denoise.metallib", @"DenoiseMain");
                photonClearPipeline = Pipeline(config, "photon-clear.metallib", @"PhotonClear");
                photonPipeline      = Pipeline(config, "photon-trace.metallib", @"PhotonMain");
                aoPipelines[0]      = Pipeline(config, "ao-prepare.metallib", @"AOPrepare");
                aoPipelines[1]      = Pipeline(config, "ao-pyramid.metallib", @"AOPyramid");
                aoPipelines[2]      = Pipeline(config, "ao-main.metallib", @"AOMain");
                aoPipelines[3]      = Pipeline(config, "ao-filter.metallib", @"AOFilter");
                mltPipelines[0]     = Pipeline(config, "mlt-bootstrap.metallib", @"MLTBootstrap");
                mltPipelines[1]     = Pipeline(config, "mlt-reduce.metallib", @"MLTReduce");
                mltPipelines[2]     = Pipeline(config, "mlt-mutate.metallib", @"MLTMutate");
            }
            void SetScene(const PathScene& scene) override
            {
                vertices      = Buffer(scene.model.vertices.data(), scene.model.vertices.size() * sizeof(ModelVertex));
                texels        = Buffer(scene.texels.data(), scene.texels.size());
                sky           = Buffer(scene.sky.pixels.data(), scene.sky.pixels.size() * 2);
                auto geometry = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
                geometry.vertexBuffer         = vertices;
                geometry.vertexStride         = sizeof(ModelVertex);
                geometry.vertexFormat         = MTLAttributeFormatFloat3;
                geometry.triangleCount        = scene.model.vertices.size() / 3;
                geometry.opaque               = YES;
                auto primitive                = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
                primitive.geometryDescriptors = @[ geometry ];
                blas                          = Build(primitive);
                MTLAccelerationStructureInstanceDescriptor instance{};
                instance.transformationMatrix.columns[0] = MTLPackedFloat3{1, 0, 0};
                instance.transformationMatrix.columns[1] = MTLPackedFloat3{0, 1, 0};
                instance.transformationMatrix.columns[2] = MTLPackedFloat3{0, 0, 1};
                instance.mask                            = 255;
                instance.options                         = MTLAccelerationStructureInstanceOptionOpaque;
                auto instances                           = Buffer(&instance, sizeof(instance));
                auto top                                 = [MTLInstanceAccelerationStructureDescriptor descriptor];
                top.instancedAccelerationStructures      = @[ blas ];
                top.instanceDescriptorBuffer             = instances;
                top.instanceCount                        = 1;
                tlas                                     = Build(top);
            }
            std::vector<unsigned char> Trace(const PathUniforms& uniforms) override
            {
                if (
                  width != uniforms.frame[1] || height != uniforms.frame[2]
                  || scratchBytes != PathScratchBytes(uniforms))
                {
                    scratchBytes = PathScratchBytes(uniforms);
                    width        = uniforms.frame[1];
                    height       = uniforms.frame[2];
                    accumulation = [device newBufferWithLength:size_t(width) * height * 16
                                                       options:MTLResourceStorageModePrivate];
                    output       = [device newBufferWithLength:size_t(width) * height * 4
                                                 options:MTLResourceStorageModeShared];
                    guides       = [device newBufferWithLength:scratchBytes options:MTLResourceStorageModePrivate];
                    if (!accumulation || !output || !guides)
                        throw std::runtime_error("Metal path image allocation failed");
                }
                auto command = [queue commandBuffer];
                auto encoder = [command computeCommandEncoder];
                [encoder setComputePipelineState:pipeline];
                [encoder setAccelerationStructure:tlas atBufferIndex:0];
                [encoder useResource:blas usage:MTLResourceUsageRead];
                std::memcpy(params.contents, &uniforms, sizeof(uniforms));
                [encoder setBuffer:params offset:0 atIndex:1];
                [encoder setBuffer:vertices offset:0 atIndex:2];
                [encoder setBuffer:texels offset:0 atIndex:3];
                [encoder setBuffer:sky offset:0 atIndex:4];
                [encoder setBuffer:accumulation offset:0 atIndex:5];
                [encoder setBuffer:output offset:0 atIndex:6];
                [encoder setBuffer:guides offset:0 atIndex:7];
                if ((uniforms.options[3] & 64u) && !uniforms.frame[0])
                {
                    [encoder setComputePipelineState:aoPipelines[0]];
                    [encoder dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                    [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
                    uint32_t method;
                    std::memcpy(&method, &uniforms.clear[3], 4);
                    if (method == 3)
                    {
                        [encoder setComputePipelineState:aoPipelines[1]];
                        [encoder dispatchThreads:MTLSizeMake(width, height, 1)
                           threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                        [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
                    }
                }
                [encoder setComputePipelineState:(uniforms.options[3] & 64u) ? aoPipelines[2] : pipeline];
                if (uniforms.options[3] & 32u)
                {
                    uint32_t boot;
                    std::memcpy(&boot, &uniforms.clear[3], 4);
                    for (int stage = uniforms.frame[0] ? 2 : 0; stage < 3; stage++)
                    {
                        [encoder setComputePipelineState:mltPipelines[stage]];
                        const uint32_t count = stage == 0   ? std::max(width * height, boot)
                                               : stage == 1 ? 1
                                                            : std::min(1024u, width * height);
                        [encoder dispatchThreads:MTLSizeMake(
                                                   stage == 0 ? std::min(count, 65536u) : count,
                                                   stage == 0 ? (count + 65535) / 65536 : 1,
                                                   1)
                           threadsPerThreadgroup:MTLSizeMake(stage == 1 ? 1 : 64, 1, 1)];
                        [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
                    }
                    [encoder setComputePipelineState:pipeline];
                }
                if (uniforms.options[3] & 16u)
                {
                    [encoder setComputePipelineState:photonClearPipeline];
                    [encoder dispatchThreads:MTLSizeMake(32768, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                    [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
                    [encoder setComputePipelineState:photonPipeline];
                    [encoder dispatchThreads:MTLSizeMake(uniforms.images[2][3], 1, 1)
                       threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                    [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
                    [encoder setComputePipelineState:pipeline];
                }
                [encoder dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                [encoder endEncoding];
                if (uniforms.options[3] & 1u)
                {
                    auto filter = [command computeCommandEncoder];
                    [filter setComputePipelineState:(uniforms.options[3] & 64u) ? aoPipelines[3] : denoisePipeline];
                    [filter setBuffer:params offset:0 atIndex:1];
                    [filter setBuffer:accumulation offset:0 atIndex:5];
                    [filter setBuffer:output offset:0 atIndex:6];
                    [filter setBuffer:guides offset:0 atIndex:7];
                    [filter dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                    [filter endEncoding];
                }
                Finish(command);
                std::vector<unsigned char> pixels(size_t(width) * height * 4);
                std::memcpy(pixels.data(), output.contents, pixels.size());
                return pixels;
            }
        };
    } // namespace
    std::unique_ptr<PathDevice> CreateMetalPathDevice(const RendererConfig& config)
    {
        return std::make_unique<MetalPath>(config);
    }
} // namespace render::detail
