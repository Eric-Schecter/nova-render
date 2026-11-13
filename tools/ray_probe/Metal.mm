#include "Result.hpp"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv)
{
    @autoreleasepool
    {
        try
        {
            if (argc != 2)
                throw std::runtime_error("Expected translated metallib path");
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device)
            {
                std::cout << "SKIP: no Metal device\n";
                return 77;
            }
            std::cout << "Metal device: " << device.name.UTF8String
                      << "; supportsRaytracing=" << device.supportsRaytracing << '\n';
            if (!device.supportsRaytracing)
                return 77;
            id<MTLCommandQueue> queue  = [device newCommandQueue];
            auto                finish = [](id<MTLCommandBuffer> command) {
                [command commit];
                [command waitUntilCompleted];
                if (command.status != MTLCommandBufferStatusCompleted)
                    throw std::runtime_error(command.error.localizedDescription.UTF8String ?: "Metal command failed");
            };
            const float   vertices[]      = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
            id<MTLBuffer> vertexBuffer    = [device newBufferWithBytes:vertices
                                                             length:sizeof(vertices)
                                                            options:MTLResourceStorageModeShared];
            auto          geometry        = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
            geometry.vertexBuffer         = vertexBuffer;
            geometry.vertexStride         = 12;
            geometry.vertexFormat         = MTLAttributeFormatFloat3;
            geometry.triangleCount        = 1;
            geometry.opaque               = YES;
            auto primitive                = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
            primitive.geometryDescriptors = @[ geometry ];
            auto build                    = [&](MTLAccelerationStructureDescriptor* desc) {
                const auto                   sizes = [device accelerationStructureSizesWithDescriptor:desc];
                id<MTLAccelerationStructure> structure =
                  [device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
                id<MTLBuffer> scratch = [device newBufferWithLength:sizes.buildScratchBufferSize
                                                            options:MTLResourceStorageModePrivate];
                if (!structure || !scratch)
                    throw std::runtime_error("Metal AS allocation failed");
                id<MTLCommandBuffer>                       command = [queue commandBuffer];
                id<MTLAccelerationStructureCommandEncoder> encoder = [command accelerationStructureCommandEncoder];
                [encoder buildAccelerationStructure:structure
                                         descriptor:desc
                                      scratchBuffer:scratch
                                scratchBufferOffset:0];
                [encoder endEncoding];
                finish(command);
                return structure;
            };
            id<MTLAccelerationStructure>               blas = build(primitive);
            MTLAccelerationStructureInstanceDescriptor instance{};
            instance.transformationMatrix.columns[0] = MTLPackedFloat3{1, 0, 0};
            instance.transformationMatrix.columns[1] = MTLPackedFloat3{0, 1, 0};
            instance.transformationMatrix.columns[2] = MTLPackedFloat3{0, 0, 1};
            instance.mask                            = 255;
            instance.options                         = MTLAccelerationStructureInstanceOptionOpaque;
            id<MTLBuffer> instances                  = [device newBufferWithBytes:&instance
                                                          length:sizeof(instance)
                                                         options:MTLResourceStorageModeShared];
            auto          top                        = [MTLInstanceAccelerationStructureDescriptor descriptor];
            top.instancedAccelerationStructures      = @[ blas ];
            top.instanceDescriptorBuffer             = instances;
            top.instanceCount                        = 1;
            id<MTLAccelerationStructure> tlas        = build(top);
            NSError*                     error       = nil;
            id<MTLLibrary>               library =
              [device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]] error:&error];
            if (!library)
                throw std::runtime_error(error.localizedDescription.UTF8String);
            id<MTLComputePipelineState> pipeline =
              [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"CSMain"] error:&error];
            if (!pipeline)
                throw std::runtime_error(error.localizedDescription.UTF8String);
            id<MTLBuffer> output = [device newBufferWithLength:probe::ResultBytes options:MTLResourceStorageModeShared];
            std::memset(output.contents, 0xff, probe::ResultBytes);
            id<MTLCommandBuffer>         command = [queue commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            [encoder setComputePipelineState:pipeline];
            [encoder setAccelerationStructure:tlas atBufferIndex:0];
            [encoder useResource:blas usage:MTLResourceUsageRead];
            [encoder setBuffer:output offset:0 atIndex:1];
            [encoder dispatchThreads:MTLSizeMake(4, 1, 1) threadsPerThreadgroup:MTLSizeMake(4, 1, 1)];
            [encoder endEncoding];
            finish(command);
            probe::Check(static_cast<const uint32_t*>(output.contents));
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
}
