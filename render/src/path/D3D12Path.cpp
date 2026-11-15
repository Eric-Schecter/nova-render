#include "D3D12Debug.hpp"
#include "PathTracing.hpp"
#include "ShaderData.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <stdexcept>
#include <wrl/client.h>
namespace render::detail
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        void Check(HRESULT result)
        {
            if (FAILED(result))
                throw std::runtime_error("DXR call failed: " + std::to_string(result));
        }
        class D3D12Path final : public PathDevice
        {
            ComPtr<ID3D12Device5>              device;
            ComPtr<ID3D12CommandQueue>         queue;
            ComPtr<ID3D12CommandAllocator>     allocator;
            ComPtr<ID3D12GraphicsCommandList4> commands;
            ComPtr<ID3D12Fence>                fence;
            ComPtr<ID3D12RootSignature>        root;
            ComPtr<ID3D12PipelineState>        mltPipelines[3], aoPipelines[4];
            ComPtr<ID3D12PipelineState>        pipeline, denoisePipeline, photonClearPipeline, photonPipeline;
            ComPtr<ID3D12Resource> params, vertices, texels, sky, blas, tlas, instances, scratch, accumulation, output,
              readback, guides;
            uint64_t               signal = 0;
            uint32_t               width = 0, height = 0;
            bool                   copied       = false;
            size_t                 scratchBytes = 0;
            ComPtr<ID3D12Resource> Buffer(
              UINT64                size,
              D3D12_HEAP_TYPE       type,
              D3D12_RESOURCE_STATES state,
              D3D12_RESOURCE_FLAGS  flags = D3D12_RESOURCE_FLAG_NONE)
            {
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = type;
                D3D12_RESOURCE_DESC info{};
                info.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
                info.Width            = size;
                info.Height           = 1;
                info.DepthOrArraySize = 1;
                info.MipLevels        = 1;
                info.SampleDesc.Count = 1;
                info.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                info.Flags            = flags;
                ComPtr<ID3D12Resource> result;
                Check(device->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &info, state, nullptr, IID_PPV_ARGS(&result)));
                return result;
            }
            ComPtr<ID3D12Resource> Upload(const void* data, size_t bytes)
            {
                auto        result = Buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
                void*       mapped;
                D3D12_RANGE empty{0, 0};
                Check(result->Map(0, &empty, &mapped));
                std::memcpy(mapped, data, bytes);
                result->Unmap(0, nullptr);
                return result;
            }
            void Begin()
            {
                Check(allocator->Reset());
                Check(commands->Reset(allocator.Get(), nullptr));
            }
            void Barrier(ID3D12Resource* resource)
            {
                D3D12_RESOURCE_BARRIER b{};
                b.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                b.UAV.pResource = resource;
                commands->ResourceBarrier(1, &b);
            }
            void Submit()
            {
                Check(commands->Close());
                ID3D12CommandList* list = commands.Get();
                queue->ExecuteCommandLists(1, &list);
                Check(queue->Signal(fence.Get(), ++signal));
                const auto started = std::chrono::steady_clock::now();
                while (fence->GetCompletedValue() < signal)
                {
                    if (std::chrono::steady_clock::now() - started > std::chrono::seconds(60))
                        throw std::runtime_error("DXR wait timed out");
                    Sleep(1);
                }
                Check(device->GetDeviceRemovedReason());
                ComPtr<ID3D12InfoQueue> messages;
                if (SUCCEEDED(device.As(&messages)))
                {
                    for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i)
                    {
                        SIZE_T size = 0;
                        messages->GetMessage(i, nullptr, &size);
                        std::vector<unsigned char> bytes(size);
                        auto*                      message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
                        Check(messages->GetMessage(i, message, &size));
                        if (
                          message->Severity == D3D12_MESSAGE_SEVERITY_ERROR
                          || message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
                            throw std::runtime_error(message->pDescription);
                    }
                    messages->ClearStoredMessages();
                }
            }

          public:
            explicit D3D12Path(const RendererConfig& config)
            {
                ConfigureD3D12DebugLayer(config.enableValidation);
                ComPtr<IDXGIFactory6> factory;
                Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
                ComPtr<IDXGIAdapter1> adapter;
                if (config.useWarp)
                    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
                else
                    Check(factory->EnumAdapterByGpuPreference(
                      0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)));
                if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))))
                    throw std::runtime_error("DXR requires a feature-level 12.0 device");
                D3D12_FEATURE_DATA_D3D12_OPTIONS5 features{};
                Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features, sizeof(features)));
                D3D12_FEATURE_DATA_SHADER_MODEL shader{D3D_SHADER_MODEL_6_5};
                if (
                  features.RaytracingTier < D3D12_RAYTRACING_TIER_1_1
                  || FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader, sizeof(shader)))
                  || shader.HighestShaderModel < D3D_SHADER_MODEL_6_5)
                    throw std::runtime_error("DXR tier 1.1 / SM6.5 unavailable");
                DXGI_ADAPTER_DESC1 desc;
                Check(adapter->GetDesc1(&desc));
                char name[512]{};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
                capabilities.pathTracing        = true;
                capabilities.softwareRayTracing = bool(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE);
                capabilities.rayTracingDevice   = name;
                D3D12_COMMAND_QUEUE_DESC q{};
                q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                Check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
                Check(device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&allocator)));
                Check(device->CreateCommandList(0, q.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)));
                Check(commands->Close());
                Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
                D3D12_ROOT_PARAMETER bindings[8]{};
                for (UINT i = 0; i < 8; ++i)
                {
                    bindings[i].ParameterType = i < 5 ? D3D12_ROOT_PARAMETER_TYPE_SRV : D3D12_ROOT_PARAMETER_TYPE_UAV;
                    bindings[i].Descriptor.ShaderRegister = i < 5 ? i : i - 5;
                }
                D3D12_ROOT_SIGNATURE_DESC rootDesc{};
                rootDesc.NumParameters = 8;
                rootDesc.pParameters   = bindings;
                ComPtr<ID3DBlob> blob, error;
                Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
                Check(
                  device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)));
                const auto                        code = LoadShader(config.shaderDirectory, "pathtrace.compute.dxil");
                D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
                pso.pRootSignature = root.Get();
                pso.CS             = {code.data(), code.size()};
                Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipeline)));
                const auto filterCode = LoadShader(config.shaderDirectory, "denoise.compute.dxil");
                pso.CS                = {filterCode.data(), filterCode.size()};
                Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&denoisePipeline)));
                const auto clearCode = LoadShader(config.shaderDirectory, "photon-clear.compute.dxil");
                pso.CS               = {clearCode.data(), clearCode.size()};
                Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&photonClearPipeline)));
                const auto photonCode = LoadShader(config.shaderDirectory, "photon-trace.compute.dxil");
                pso.CS                = {photonCode.data(), photonCode.size()};
                Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&photonPipeline)));
                const char* mltFiles[] = {
                  "mlt-bootstrap.compute.dxil", "mlt-reduce.compute.dxil", "mlt-mutate.compute.dxil"};
                for (int i = 0; i < 3; i++)
                {
                    const auto bytes = LoadShader(config.shaderDirectory, mltFiles[i]);
                    pso.CS           = {bytes.data(), bytes.size()};
                    Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&mltPipelines[i])));
                }
                const char* aoFiles[] = {
                  "ao-prepare.compute.dxil",
                  "ao-pyramid.compute.dxil",
                  "ao-main.compute.dxil",
                  "ao-filter.compute.dxil"};
                for (int i = 0; i < 4; i++)
                {
                    const auto bytes = LoadShader(config.shaderDirectory, aoFiles[i]);
                    pso.CS           = {bytes.data(), bytes.size()};
                    Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&aoPipelines[i])));
                }

                const PathUniforms zero{};
                params = Upload(&zero, sizeof(zero));
            }
            void SetScene(const PathScene& scene) override
            {
                vertices = Upload(scene.model.vertices.data(), scene.model.vertices.size() * sizeof(ModelVertex));
                texels   = Upload(scene.texels.data(), scene.texels.size());
                sky      = Upload(scene.sky.pixels.data(), scene.sky.pixels.size() * 2);
                D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
                geometry.Type                   = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
                geometry.Flags                  = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
                geometry.Triangles.VertexBuffer = {vertices->GetGPUVirtualAddress(), sizeof(ModelVertex)};
                geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
                geometry.Triangles.VertexCount  = UINT(scene.model.vertices.size());
                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom{};
                bottom.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
                bottom.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
                bottom.NumDescs       = 1;
                bottom.pGeometryDescs = &geometry;
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes{};
                device->GetRaytracingAccelerationStructurePrebuildInfo(&bottom, &sizes);
                blas = Buffer(
                  sizes.ResultDataMaxSizeInBytes,
                  D3D12_HEAP_TYPE_DEFAULT,
                  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                D3D12_RAYTRACING_INSTANCE_DESC instance{};
                instance.Transform[0][0] = instance.Transform[1][1] = instance.Transform[2][2] = 1;
                instance.InstanceMask                                                          = 255;
                instance.AccelerationStructure = blas->GetGPUVirtualAddress();
                instances                      = Upload(&instance, sizeof(instance));
                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{};
                top.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
                top.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
                top.NumDescs      = 1;
                top.InstanceDescs = instances->GetGPUVirtualAddress();
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topSizes{};
                device->GetRaytracingAccelerationStructurePrebuildInfo(&top, &topSizes);
                tlas = Buffer(
                  topSizes.ResultDataMaxSizeInBytes,
                  D3D12_HEAP_TYPE_DEFAULT,
                  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                scratch = Buffer(
                  std::max(sizes.ScratchDataSizeInBytes, topSizes.ScratchDataSizeInBytes),
                  D3D12_HEAP_TYPE_DEFAULT,
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                Begin();
                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
                build.Inputs                           = bottom;
                build.DestAccelerationStructureData    = blas->GetGPUVirtualAddress();
                build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
                commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
                Barrier(blas.Get());
                Barrier(scratch.Get());
                build.Inputs                        = top;
                build.DestAccelerationStructureData = tlas->GetGPUVirtualAddress();
                commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
                Barrier(tlas.Get());
                Submit();
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
                    copied       = false;
                    accumulation = Buffer(
                      size_t(width) * height * 16,
                      D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                    output = Buffer(
                      size_t(width) * height * 4,
                      D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                    guides = Buffer(
                      scratchBytes,
                      D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                    readback =
                      Buffer(size_t(width) * height * 4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
                }
                void*       mapped;
                D3D12_RANGE empty{0, 0};
                Check(params->Map(0, &empty, &mapped));
                std::memcpy(mapped, &uniforms, sizeof(uniforms));
                params->Unmap(0, nullptr);
                Begin();
                Barrier(accumulation.Get());
                Barrier(guides.Get());
                D3D12_RESOURCE_BARRIER transition{};
                transition.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                transition.Transition.pResource   = output.Get();
                transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                if (copied)
                {
                    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                    transition.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    commands->ResourceBarrier(1, &transition);
                }
                commands->SetComputeRootSignature(root.Get());
                commands->SetPipelineState(pipeline.Get());
                commands->SetComputeRootShaderResourceView(0, tlas->GetGPUVirtualAddress());
                ID3D12Resource* inputs[] = {params.Get(), vertices.Get(), texels.Get(), sky.Get()};
                for (UINT i = 0; i < 4; ++i)
                    commands->SetComputeRootShaderResourceView(i + 1, inputs[i]->GetGPUVirtualAddress());
                commands->SetComputeRootUnorderedAccessView(5, accumulation->GetGPUVirtualAddress());
                commands->SetComputeRootUnorderedAccessView(6, output->GetGPUVirtualAddress());
                commands->SetComputeRootUnorderedAccessView(7, guides->GetGPUVirtualAddress());
                if ((uniforms.options[3] & 64u) && !uniforms.frame[0])
                {
                    commands->SetPipelineState(aoPipelines[0].Get());
                    commands->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                    Barrier(guides.Get());
                    uint32_t method;
                    std::memcpy(&method, &uniforms.clear[3], 4);
                    if (method == 3)
                    {
                        commands->SetPipelineState(aoPipelines[1].Get());
                        commands->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                        Barrier(guides.Get());
                    }
                }
                commands->SetPipelineState((uniforms.options[3] & 64u) ? aoPipelines[2].Get() : pipeline.Get());
                if (uniforms.options[3] & 32u)
                {
                    uint32_t boot;
                    std::memcpy(&boot, &uniforms.clear[3], 4);
                    for (int stage = uniforms.frame[0] ? 2 : 0; stage < 3; stage++)
                    {
                        commands->SetPipelineState(mltPipelines[stage].Get());
                        const uint32_t count = stage == 0   ? std::max(width * height, boot)
                                               : stage == 1 ? 1
                                                            : std::min(1024u, width * height);
                        commands->Dispatch(
                          stage == 0   ? std::min((count + 63) / 64, 1024u)
                          : stage == 1 ? 1
                                       : (count + 63) / 64,
                          stage == 0 ? (count + 65535) / 65536 : 1,
                          1);
                        Barrier(guides.Get());
                    }
                    commands->SetPipelineState(pipeline.Get());
                }
                if (uniforms.options[3] & 16u)
                {
                    commands->SetPipelineState(photonClearPipeline.Get());
                    commands->Dispatch(512, 1, 1);
                    Barrier(guides.Get());
                    commands->SetPipelineState(photonPipeline.Get());
                    commands->Dispatch((uniforms.images[2][3] + 63) / 64, 1, 1);
                    Barrier(guides.Get());
                    commands->SetPipelineState(pipeline.Get());
                }
                commands->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                if (uniforms.options[3] & 1u)
                {
                    Barrier(accumulation.Get());
                    Barrier(guides.Get());
                    Barrier(output.Get());
                    commands->SetPipelineState(
                      (uniforms.options[3] & 64u) ? aoPipelines[3].Get() : denoisePipeline.Get());
                    commands->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                }
                transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                transition.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
                commands->ResourceBarrier(1, &transition);
                commands->CopyResource(readback.Get(), output.Get());
                Submit();
                copied = true;
                std::vector<unsigned char> pixels(size_t(width) * height * 4);
                D3D12_RANGE                range{0, pixels.size()};
                Check(readback->Map(0, &range, &mapped));
                std::memcpy(pixels.data(), mapped, pixels.size());
                readback->Unmap(0, &empty);
                return pixels;
            }
        };
    } // namespace
    std::unique_ptr<PathDevice> CreateD3D12PathDevice(const RendererConfig& config)
    {
        return std::make_unique<D3D12Path>(config);
    }
} // namespace render::detail
