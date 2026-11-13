#pragma once
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
namespace render::detail
{
    // The D3D12 debug layer is process-wide and must be configured before any device.
    inline bool ConfigureD3D12DebugLayer(bool request)
    {
        static const bool enabled = [request] {
            if (!request)
                return false;
            Microsoft::WRL::ComPtr<ID3D12Debug> debug;
            if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
                return false;
            debug->EnableDebugLayer();
            return true;
        }();
        return enabled;
    }
} // namespace render::detail
