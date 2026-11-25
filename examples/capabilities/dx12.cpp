#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

void PrintDX12Devices()
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        throw std::runtime_error("Create DXGI factory failed");
    std::cout << "\nDX12 adapters\n";
    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;
        const auto result = factory->EnumAdapters1(i, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(result))
            throw std::runtime_error("Enumerate DXGI adapter failed");
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)))
            continue;
        std::wcout << L"\nDevice = " << desc.Description << L'\n';
        std::cout << "Vendor ID = " << desc.VendorId << "\nDevice ID = " << desc.DeviceId
                  << "\nDedicated video memory (bytes) = " << desc.DedicatedVideoMemory
                  << "\nShared system memory (bytes) = " << desc.SharedSystemMemory
                  << "\nSoftware adapter = " << bool(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) << '\n';
        ComPtr<IDXGIAdapter3> budgetAdapter;
        DXGI_QUERY_VIDEO_MEMORY_INFO budget{};
        if (SUCCEEDED(adapter.As(&budgetAdapter))
            && SUCCEEDED(budgetAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &budget)))
            std::cout << "Local memory budget (bytes) = " << budget.Budget
                      << "\nLocal memory usage (bytes) = " << budget.CurrentUsage << '\n';
        ComPtr<ID3D12Device> device;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        {
            std::cout << "DX12 unavailable\n";
            continue;
        }
        D3D12_FEATURE_DATA_SHADER_MODEL shader{D3D_SHADER_MODEL_6_5};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader, sizeof(shader))))
            std::cout << "Shader Model (capped at 6.5 enum) = " << shader.HighestShaderModel << '\n';
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
            std::cout << "ResourceBindingTier = " << options.ResourceBindingTier
                      << "\nResourceHeapTier = " << options.ResourceHeapTier
                      << "\nTiledResourcesTier = " << options.TiledResourcesTier << '\n';
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 rays{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &rays, sizeof(rays))))
            std::cout << "RaytracingTier = " << rays.RaytracingTier << '\n';
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 mesh{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &mesh, sizeof(mesh))))
            std::cout << "MeshShaderTier = " << mesh.MeshShaderTier
                      << "\nSamplerFeedbackTier = " << mesh.SamplerFeedbackTier << '\n';
        for (UINT samples : {1u, 2u, 4u, 8u})
        {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaa{DXGI_FORMAT_R16G16B16A16_FLOAT, samples};
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaa, sizeof(msaa))))
                std::cout << "RGBA16F sample count " << samples << " = " << (msaa.NumQualityLevels != 0) << '\n';
        }
    }
}
