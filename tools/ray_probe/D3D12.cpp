#include "Result.hpp"
#include <array>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace
{
    void Check(HRESULT result)
    {
        if (FAILED(result))
            throw std::runtime_error("D3D12 call failed: " + std::to_string(result));
    }
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
            throw std::runtime_error("Expected DXIL path, optional --warp");
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory;
        Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter1> adapter;
        const bool            warp = argc > 2 && std::string(argv[2]) == "--warp";
        if (warp)
            Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        else
            Check(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)));
        DXGI_ADAPTER_DESC1 desc;
        Check(adapter->GetDesc1(&desc));
        std::wcout << L"D3D12 device: " << desc.Description << L"; software="
                   << bool(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) << L'\n';
        ComPtr<ID3D12Device5> device;
        const auto            created = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
        if (FAILED(created))
        {
            std::cout << "SKIP: adapter has no D3D12 feature level 12.0 device\n";
            return 77;
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 features{};
        Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features, sizeof(features)));
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_5};
        const auto shaderResult = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
        std::cout << "RaytracingTier=" << features.RaytracingTier
                  << "; SM6.5=" << (SUCCEEDED(shaderResult) && model.HighestShaderModel >= D3D_SHADER_MODEL_6_5)
                  << '\n';
        if (
          features.RaytracingTier < D3D12_RAYTRACING_TIER_1_1 || FAILED(shaderResult)
          || model.HighestShaderModel < D3D_SHADER_MODEL_6_5)
        {
            std::cout << "SKIP: inline DXR requires tier 1.1 and SM6.5\n";
            return 77;
        }
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue;
        Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12CommandAllocator> allocator;
        Check(device->CreateCommandAllocator(queueDesc.Type, IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList4> commands;
        Check(device->CreateCommandList(0, queueDesc.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)));
        ComPtr<ID3D12Fence> fence;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        auto submit = [&] {
            Check(commands->Close());
            ID3D12CommandList* list = commands.Get();
            queue->ExecuteCommandLists(1, &list);
            Check(queue->Signal(fence.Get(), 1));
            while (fence->GetCompletedValue() < 1)
                Sleep(1);
        };
        auto buffer = [&](
                        UINT64                size,
                        D3D12_HEAP_TYPE       type,
                        D3D12_RESOURCE_STATES state,
                        D3D12_RESOURCE_FLAGS  flags = D3D12_RESOURCE_FLAG_NONE) {
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
        };
        auto upload = [&](const void* data, size_t size) {
            auto  result = buffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            void* mapped;
            Check(result->Map(0, nullptr, &mapped));
            std::memcpy(mapped, data, size);
            result->Unmap(0, nullptr);
            return result;
        };
        const float                    vertices[] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
        auto                           vertex     = upload(vertices, sizeof(vertices));
        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
        geometry.Type                   = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry.Flags                  = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.Triangles.VertexBuffer = {vertex->GetGPUVirtualAddress(), 12};
        geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.Triangles.VertexCount  = 3;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom{};
        bottom.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottom.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bottom.NumDescs       = 1;
        bottom.pGeometryDescs = &geometry;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes{};
        device->GetRaytracingAccelerationStructurePrebuildInfo(&bottom, &sizes);
        auto blas = buffer(
          sizes.ResultDataMaxSizeInBytes,
          D3D12_HEAP_TYPE_DEFAULT,
          D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        D3D12_RAYTRACING_INSTANCE_DESC instance{};
        instance.Transform[0][0] = instance.Transform[1][1] = instance.Transform[2][2] = 1;
        instance.InstanceMask                                                          = 255;
        instance.AccelerationStructure                                                 = blas->GetGPUVirtualAddress();
        auto                                                 instances = upload(&instance, sizeof(instance));
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{};
        top.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        top.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
        top.NumDescs      = 1;
        top.InstanceDescs = instances->GetGPUVirtualAddress();
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topSizes{};
        device->GetRaytracingAccelerationStructurePrebuildInfo(&top, &topSizes);
        auto tlas = buffer(
          topSizes.ResultDataMaxSizeInBytes,
          D3D12_HEAP_TYPE_DEFAULT,
          D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto scratch = buffer(
          (sizes.ScratchDataSizeInBytes > topSizes.ScratchDataSizeInBytes ? sizes.ScratchDataSizeInBytes
                                                                          : topSizes.ScratchDataSizeInBytes),
          D3D12_HEAP_TYPE_DEFAULT,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto barrier = [&](ID3D12Resource* resource) {
            D3D12_RESOURCE_BARRIER b{};
            b.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            b.UAV.pResource = resource;
            commands->ResourceBarrier(1, &b);
        };
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
        build.Inputs                           = bottom;
        build.DestAccelerationStructureData    = blas->GetGPUVirtualAddress();
        build.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
        commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
        barrier(blas.Get());
        barrier(scratch.Get());
        build.Inputs                        = top;
        build.DestAccelerationStructureData = tlas->GetGPUVirtualAddress();
        commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
        barrier(tlas.Get());
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 2;
        rootDesc.pParameters   = params;
        ComPtr<ID3DBlob> blob, error;
        Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
        ComPtr<ID3D12RootSignature> root;
        Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)));
        std::ifstream     file(argv[1], std::ios::binary);
        std::vector<char> code((std::istreambuf_iterator<char>(file)), {});
        if (code.empty())
            throw std::runtime_error("Missing DXIL");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
        pipelineDesc.pRootSignature = root.Get();
        pipelineDesc.CS             = {code.data(), code.size()};
        ComPtr<ID3D12PipelineState> pipeline;
        Check(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)));
        auto output = buffer(
          probe::ResultBytes,
          D3D12_HEAP_TYPE_DEFAULT,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto readback = buffer(probe::ResultBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        commands->SetComputeRootSignature(root.Get());
        commands->SetPipelineState(pipeline.Get());
        commands->SetComputeRootShaderResourceView(0, tlas->GetGPUVirtualAddress());
        commands->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
        commands->Dispatch(1, 1, 1);
        D3D12_RESOURCE_BARRIER transition{};
        transition.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition.pResource   = output.Get();
        transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        transition.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands->ResourceBarrier(1, &transition);
        commands->CopyResource(readback.Get(), output.Get());
        submit();
        void*       mapped;
        D3D12_RANGE range{0, probe::ResultBytes};
        Check(readback->Map(0, &range, &mapped));
        probe::Check(static_cast<const uint32_t*>(mapped));
        readback->Unmap(0, nullptr);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
