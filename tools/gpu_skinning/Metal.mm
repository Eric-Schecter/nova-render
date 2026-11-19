#include "AnimationData.hpp"
#include "SkinReference.hpp"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv)
{
    @autoreleasepool
    {
        try
        {
            if (argc != 3)
                return 1;
            auto device = MTLCreateSystemDefaultDevice();
            if (!device)
                return 77;
            NSError* error   = nil;
            auto     library = [device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[2]]]
                                               error:&error];
            if (!library)
                throw std::runtime_error(error.localizedDescription.UTF8String);
            auto pipeline = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"SkinningCSMain"]
                                                                  error:&error];
            if (!pipeline)
                throw std::runtime_error(error.localizedDescription.UTF8String);
            auto                            queue = [device newCommandQueue];
            render::detail::AnimationPlayer player;
            player.Load(render::detail::LoadGLB(argv[1]));
            double maximum = 0;
            int    poses   = 0;
            for (uint32_t clip = 0; clip < 2; ++clip)
                for (float time : {0.f, .25f, .5f, 1.25f, 2.f})
                    for (bool scaled : {false, true})
                    {
                        player.Select(clip);
                        player.Seek(time);
                        auto data = *player.Model();
                        if (scaled)
                            for (auto& matrix : data.jointMatrices)
                            {
                                const auto value =
                                  glm::scale(glm::mat4(1), glm::vec3(-1.2f, .8f, 1.1f)) * glm::make_mat4(matrix.data());
                                std::memcpy(matrix.data(), glm::value_ptr(value), 64);
                            }
                        auto make = [&](const void* bytes, size_t size) {
                            auto result = [device newBufferWithBytes:bytes
                                                              length:size
                                                             options:MTLResourceStorageModeShared];
                            if (!result)
                                throw std::runtime_error("Allocate skin probe buffer failed");
                            return result;
                        };
                        render::detail::SceneUniforms scene{};
                        scene.skyParams[3] = 1;
                        scene.skyParams[2] = float(data.vertices.size());
                        auto params        = make(&scene, sizeof(scene)),
                             vertices = make(data.vertices.data(), data.vertices.size() * sizeof(data.vertices[0])),
                             inputs = make(data.skinInputs.data(), data.skinInputs.size() * sizeof(data.skinInputs[0])),
                             joints = make(
                               data.jointMatrices.data(), data.jointMatrices.size() * sizeof(data.jointMatrices[0]));
                        auto output  = [device newBufferWithLength:data.vertices.size() * sizeof(data.vertices[0])
                                                          options:MTLResourceStorageModeShared];
                        auto command = [queue commandBuffer];
                        auto encoder = [command computeCommandEncoder];
                        [encoder setComputePipelineState:pipeline];
                        [encoder setBuffer:params offset:0 atIndex:0];
                        [encoder setBuffer:vertices offset:0 atIndex:1];
                        [encoder setBuffer:inputs offset:0 atIndex:11];
                        [encoder setBuffer:joints offset:0 atIndex:12];
                        [encoder setBuffer:output offset:0 atIndex:13];
                        [encoder dispatchThreads:MTLSizeMake(data.vertices.size(), 1, 1)
                           threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                        [encoder endEncoding];
                        [command commit];
                        [command waitUntilCompleted];
                        if (command.status != MTLCommandBufferStatusCompleted)
                            throw std::runtime_error(command.error.localizedDescription.UTF8String);
                        const auto  expected  = ReferenceSkin(data);
                        const auto* actual    = static_cast<const float*>(output.contents);
                        const auto* reference = reinterpret_cast<const float*>(expected.data());
                        for (size_t i = 0; i < expected.size() * sizeof(render::detail::ModelVertex) / sizeof(float);
                             ++i)
                        {
                            if (!std::isfinite(actual[i]))
                                throw std::runtime_error("Nonfinite GPU skin result");
                            maximum = std::max(maximum, double(std::abs(actual[i] - reference[i])));
                        }
                        ++poses;
                    }
            if (maximum > 5e-5)
                throw std::runtime_error("GPU skin output differs from CPU reference");
            std::cout << "PASS Metal GPU skin readback: " << poses
                      << " poses, mixed weights, nonuniform/reflected scale; max error=" << maximum << '\n';
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
}
