#import <Metal/Metal.h>
#include <iostream>

void PrintMetalDevices()
{
    @autoreleasepool
    {
        std::cout << "\nMetal devices\n";
        for (id<MTLDevice> device in MTLCopyAllDevices())
        {
            const auto threads = device.maxThreadsPerThreadgroup;
            std::cout << "\nDevice = " << device.name.UTF8String
                      << "\nUnified memory = " << bool(device.hasUnifiedMemory)
                      << "\nRecommended working set (bytes) = " << device.recommendedMaxWorkingSetSize
                      << "\nMaximum buffer length (bytes) = " << device.maxBufferLength
                      << "\nThreadgroup memory (bytes) = " << device.maxThreadgroupMemoryLength
                      << "\nThreadgroup dimensions = " << threads.width << 'x' << threads.height << 'x' << threads.depth
                      << "\nArgument buffers Tier 2 = " << (device.argumentBuffersSupport == MTLArgumentBuffersTier2)
                      << "\nMetal ray tracing = " << bool(device.supportsRaytracing)
                      << "\nBC compression = " << bool(device.supportsBCTextureCompression) << '\n';
            for (NSUInteger samples : {1u, 2u, 4u, 8u})
                std::cout << "Sample count " << samples << " = " << bool([device supportsTextureSampleCount:samples]) << '\n';
        }
    }
}
