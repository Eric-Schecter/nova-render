#include "../path/D3D12Debug.hpp"
#include "AADevice.hpp"
#include "ShaderData.hpp"
#include <algorithm>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdexcept>
#include <wrl/client.h>
namespace render::detail
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        void Check(HRESULT r)
        {
            if (FAILED(r))
                throw std::runtime_error("D3D12 AA call failed: " + std::to_string(r));
        }
        class D3D12AA final : public AADevice
        {
            struct Texture
            {
                ComPtr<ID3D12Resource> resource;
                D3D12_RESOURCE_STATES  state = D3D12_RESOURCE_STATE_COMMON;
                uint32_t               width = 0, height = 0;
            };
            ComPtr<ID3D12Device>              device;
            ComPtr<ID3D12CommandQueue>        queue;
            ComPtr<ID3D12CommandAllocator>    allocator;
            ComPtr<ID3D12GraphicsCommandList> commands;
            ComPtr<ID3D12Fence>               fence;
            struct Event
            {
                HANDLE value = nullptr;
                ~Event()
                {
                    if (value)
                        CloseHandle(value);
                }
            } event;
            uint64_t                           serial = 0;
            ComPtr<ID3D12RootSignature>        root;
            ComPtr<ID3D12DescriptorHeap>       rtvHeap, dsvHeap, srvHeap;
            ComPtr<ID3D12PipelineState>        pipelines[9], sceneMS;
            ComPtr<ID3D12Resource>             uniforms, readback;
            Texture                            textures[AAResourceCount], depth, depthMS, depthBaseline;
            UINT                               rtvStride = 0, dsvStride = 0, srvStride = 0;
            uint32_t                           width = 0, height = 0;
            bool                               supersampled = false;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            UINT64                             readbackSize = 0;
            ComPtr<ID3D12Resource>             Buffer(UINT64 size, D3D12_HEAP_TYPE type)
            {
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = type;
                D3D12_RESOURCE_DESC d{};
                d.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
                d.Width            = size;
                d.Height           = 1;
                d.DepthOrArraySize = d.MipLevels = 1;
                d.SampleDesc.Count               = 1;
                d.Layout                         = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                ComPtr<ID3D12Resource> b;
                Check(device->CreateCommittedResource(
                  &heap,
                  D3D12_HEAP_FLAG_NONE,
                  &d,
                  type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ,
                  nullptr,
                  IID_PPV_ARGS(&b)));
                return b;
            }
            Texture MakeTexture(uint32_t w, uint32_t h, DXGI_FORMAT format, UINT samples = 1, bool lookup = false)
            {
                Texture t;
                t.width  = w;
                t.height = h;
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_DEFAULT;
                D3D12_RESOURCE_DESC d{};
                d.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                d.Width            = w;
                d.Height           = h;
                d.DepthOrArraySize = d.MipLevels = 1;
                d.Format                         = format;
                d.SampleDesc.Count               = samples;
                const bool depth                 = format == DXGI_FORMAT_D32_FLOAT;
                d.Flags                          = lookup  ? D3D12_RESOURCE_FLAG_NONE
                                                   : depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                                                           : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                D3D12_CLEAR_VALUE clear{};
                clear.Format = format;
                if (depth)
                    clear.DepthStencil.Depth = 1;
                Check(device->CreateCommittedResource(
                  &heap,
                  D3D12_HEAP_FLAG_NONE,
                  &d,
                  D3D12_RESOURCE_STATE_COMMON,
                  lookup ? nullptr : &clear,
                  IID_PPV_ARGS(&t.resource)));
                return t;
            }
            D3D12_CPU_DESCRIPTOR_HANDLE RTV(int i)
            {
                auto h = rtvHeap->GetCPUDescriptorHandleForHeapStart();
                h.ptr += size_t(i) * rtvStride;
                return h;
            }
            D3D12_CPU_DESCRIPTOR_HANDLE DSV(int i)
            {
                auto h = dsvHeap->GetCPUDescriptorHandleForHeapStart();
                h.ptr += size_t(i) * dsvStride;
                return h;
            }
            void Transition(Texture& t, D3D12_RESOURCE_STATES state)
            {
                if (t.state == state)
                    return;
                D3D12_RESOURCE_BARRIER b{};
                b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource   = t.resource.Get();
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b.Transition.StateBefore = t.state;
                b.Transition.StateAfter  = state;
                commands->ResourceBarrier(1, &b);
                t.state = state;
            }
            void Begin()
            {
                Check(allocator->Reset());
                Check(commands->Reset(allocator.Get(), nullptr));
            }
            void End()
            {
                Check(commands->Close());
                ID3D12CommandList* lists[] = {commands.Get()};
                queue->ExecuteCommandLists(1, lists);
                Check(queue->Signal(fence.Get(), ++serial));
                if (fence->GetCompletedValue() < serial)
                {
                    Check(fence->SetEventOnCompletion(serial, event.value));
                    if (WaitForSingleObject(event.value, INFINITE) != WAIT_OBJECT_0)
                        throw std::runtime_error("D3D12 AA fence wait failed");
                }
                Check(device->GetDeviceRemovedReason());
            }
            ComPtr<ID3D12PipelineState> Pipeline(int program, UINT samples, const RendererConfig& c)
            {
                const auto vertex =
                             LoadShader(c.shaderDirectory, ("aa-" + std::to_string(program) + ".vert.dxil").c_str()),
                           fragment =
                             LoadShader(c.shaderDirectory, ("aa-" + std::to_string(program) + ".frag.dxil").c_str());
                D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
                d.pRootSignature        = root.Get();
                d.VS                    = {vertex.data(), vertex.size()};
                d.PS                    = {fragment.data(), fragment.size()};
                d.SampleMask            = UINT_MAX;
                d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                d.SampleDesc.Count      = samples;
                d.NumRenderTargets      = program == 0 ? 2 : 1;
                for (UINT i = 0; i < d.NumRenderTargets; i++)
                {
                    d.RTVFormats[i]                                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    d.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                }
                d.RasterizerState.FillMode          = D3D12_FILL_MODE_SOLID;
                d.RasterizerState.CullMode          = D3D12_CULL_MODE_NONE;
                d.RasterizerState.DepthClipEnable   = TRUE;
                d.RasterizerState.MultisampleEnable = samples > 1;
                d.DepthStencilState.DepthEnable     = program == 0;
                d.DepthStencilState.DepthWriteMask =
                  program == 0 ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
                d.DepthStencilState.DepthFunc       = D3D12_COMPARISON_FUNC_LESS;
                d.DepthStencilState.StencilReadMask = d.DepthStencilState.StencilWriteMask = 255;
                d.DepthStencilState.FrontFace.StencilFailOp   = d.DepthStencilState.FrontFace.StencilDepthFailOp =
                  d.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
                d.DepthStencilState.FrontFace.StencilFunc     = D3D12_COMPARISON_FUNC_ALWAYS;
                d.DepthStencilState.BackFace                  = d.DepthStencilState.FrontFace;
                d.DSVFormat = program == 0 ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
                ComPtr<ID3D12PipelineState> result;
                Check(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&result)));
                return result;
            }

          public:
            explicit D3D12AA(const RendererConfig& c)
            {
                ConfigureD3D12DebugLayer(c.enableValidation);
                ComPtr<IDXGIFactory4> factory;
                Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
                ComPtr<IDXGIAdapter> adapter;
                if (c.useWarp)
                    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
                Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
                for (auto format : {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D32_FLOAT})
                {
                    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{
                      format, 4, D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0};
                    Check(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q)));
                    if (!q.NumQualityLevels)
                        throw std::runtime_error("D3D12 AA requires 4x MSAA support");
                }
                D3D12_COMMAND_QUEUE_DESC q{};
                q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                Check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
                Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
                Check(device->CreateCommandList(
                  0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)));
                Check(commands->Close());
                Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
                event.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (!event.value)
                    throw std::runtime_error("D3D12 AA event creation failed");
                D3D12_DESCRIPTOR_RANGE range{};
                range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                range.NumDescriptors                    = 8;
                range.BaseShaderRegister                = 1;
                range.OffsetInDescriptorsFromTableStart = 0;
                D3D12_ROOT_PARAMETER parameters[2]{};
                parameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
                parameters[0].Descriptor.ShaderRegister = 0;
                parameters[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
                parameters[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameters[1].DescriptorTable           = {1, &range};
                parameters[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
                D3D12_STATIC_SAMPLER_DESC samplers[2]{};
                for (int i = 0; i < 2; i++)
                {
                    auto& s    = samplers[i];
                    s.Filter   = i ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
                    s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    s.MaxLOD                             = D3D12_FLOAT32_MAX;
                    s.MaxAnisotropy                      = 1;
                    s.ComparisonFunc                     = D3D12_COMPARISON_FUNC_ALWAYS;
                    s.ShaderRegister                     = 9 + i;
                    s.ShaderVisibility                   = D3D12_SHADER_VISIBILITY_PIXEL;
                }
                D3D12_ROOT_SIGNATURE_DESC signature{2, parameters, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE};
                ComPtr<ID3DBlob>          data, error;
                Check(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &data, &error));
                Check(
                  device->CreateRootSignature(0, data->GetBufferPointer(), data->GetBufferSize(), IID_PPV_ARGS(&root)));
                D3D12_DESCRIPTOR_HEAP_DESC heap{};
                heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                heap.NumDescriptors = AAResourceCount;
                Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvHeap)));
                heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                heap.NumDescriptors = 3;
                Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&dsvHeap)));
                heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                heap.NumDescriptors = 16 * 8;
                heap.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srvHeap)));
                rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                dsvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
                srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                uniforms  = Buffer(256 * 16, D3D12_HEAP_TYPE_UPLOAD);
                for (int i = 0; i < 9; i++)
                    pipelines[i] = Pipeline(i, 1, c);
                sceneMS = Pipeline(0, 4, c);
                for (int i = AAArea; i <= AASearch; i++)
                {
                    const auto table = AALookup(i == AAArea);
                    textures[i]      = MakeTexture(table.width, table.height, DXGI_FORMAT_R8G8B8A8_UNORM, 1, true);
                    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
                    UINT64                             bytes;
                    auto                               desc = textures[i].resource->GetDesc();
                    device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
                    auto        upload = Buffer(bytes, D3D12_HEAP_TYPE_UPLOAD);
                    void*       mapped;
                    D3D12_RANGE empty{0, 0};
                    Check(upload->Map(0, &empty, &mapped));
                    for (uint32_t y = 0; y < table.height; y++)
                        std::memcpy(
                          static_cast<char*>(mapped) + fp.Offset + y * fp.Footprint.RowPitch,
                          table.pixels.data() + size_t(y) * table.width * 4,
                          table.width * 4);
                    upload->Unmap(0, nullptr);
                    Begin();
                    Transition(textures[i], D3D12_RESOURCE_STATE_COPY_DEST);
                    D3D12_TEXTURE_COPY_LOCATION to{};
                    to.pResource = textures[i].resource.Get();
                    to.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    D3D12_TEXTURE_COPY_LOCATION from{};
                    from.pResource       = upload.Get();
                    from.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    from.PlacedFootprint = fp;
                    commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
                    Transition(textures[i], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    End();
                }
            }
            ~D3D12AA() override = default;
            void Resize(uint32_t w, uint32_t h, bool ss) override
            {
                if (w == width && h == height && ss == supersampled)
                    return;
                width        = w;
                height       = h;
                supersampled = ss;
                for (int i = 0; i < AAResourceCount; i++)
                    if (i != AAArea && i != AASearch)
                    {
                        const bool big = ss && (i == AAColor || i == AAMotion);
                        textures[i]    = MakeTexture(
                          i == AADummy ? 1 : w * (big ? 2 : 1),
                          i == AADummy ? 1 : h * (big ? 2 : 1),
                          DXGI_FORMAT_R16G16B16A16_FLOAT,
                          i == AAMultiColor || i == AAMultiMotion ? 4 : 1);
                        device->CreateRenderTargetView(textures[i].resource.Get(), nullptr, RTV(i));
                    }
                depth         = MakeTexture(w * (ss ? 2 : 1), h * (ss ? 2 : 1), DXGI_FORMAT_D32_FLOAT);
                depthMS       = MakeTexture(w, h, DXGI_FORMAT_D32_FLOAT, 4);
                depthBaseline = MakeTexture(w, h, DXGI_FORMAT_D32_FLOAT);
                int i         = 0;
                for (auto* d : {&depth, &depthMS, &depthBaseline})
                    device->CreateDepthStencilView(d->resource.Get(), nullptr, DSV(i++));
                auto desc = textures[AAOutput].resource->GetDesc();
                device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &readbackSize);
                readback = Buffer(readbackSize, D3D12_HEAP_TYPE_READBACK);
            }
            Image Draw(const std::vector<AAPass>& passes) override
            {
                void*       mapped;
                D3D12_RANGE empty{0, 0};
                Check(uniforms->Map(0, &empty, &mapped));
                for (size_t i = 0; i < passes.size(); i++)
                    std::memcpy(static_cast<char*>(mapped) + i * 256, passes[i].uniforms.data(), sizeof(AAUniforms));
                uniforms->Unmap(0, nullptr);
                Begin();
                for (int i : {AAHistoryA, AAHistoryB, AAPrevMotion, AADummy})
                    if (passes.front().uniforms[3][0] < .5f || i == AADummy)
                    {
                        Transition(textures[i], D3D12_RESOURCE_STATE_RENDER_TARGET);
                        const float clear[4] = {};
                        commands->ClearRenderTargetView(RTV(i), clear, 0, nullptr);
                    }
                ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
                commands->SetDescriptorHeaps(1, heaps);
                commands->SetGraphicsRootSignature(root.Get());
                commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                for (size_t index = 0; index < passes.size(); index++)
                {
                    const auto&                 p      = passes[index];
                    const bool                  scene  = p.program == 0;
                    auto&                       target = textures[p.target];
                    D3D12_CPU_DESCRIPTOR_HANDLE targets[2];
                    for (int a = 0; a < (scene ? 2 : 1); a++)
                    {
                        const int n = p.multisampled ? AAMultiColor + a : p.target + a;
                        Transition(textures[n], D3D12_RESOURCE_STATE_RENDER_TARGET);
                        targets[a]               = RTV(n);
                        const float colors[2][4] = {{.012f, .02f, .035f, .1f}, {0, 0, 1, 1}};
                        commands->ClearRenderTargetView(targets[a], colors[a], 0, nullptr);
                    }
                    auto dsv = DSV(p.target == AABaseline ? 2 : p.multisampled ? 1 : 0);
                    if (scene)
                    {
                        auto& d = p.target == AABaseline ? depthBaseline : p.multisampled ? depthMS : depth;
                        Transition(d, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                        commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
                    }
                    commands->OMSetRenderTargets(scene ? 2 : 1, targets, FALSE, scene ? &dsv : nullptr);
                    commands->SetPipelineState(p.multisampled ? sceneMS.Get() : pipelines[p.program].Get());
                    auto cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
                    cpu.ptr += index * 8 * srvStride;
                    auto gpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
                    gpu.ptr += index * 8 * srvStride;
                    for (int b = 0; b < 8; b++)
                    {
                        auto& input = textures[p.sources[b]];
                        Transition(input, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                        srv.Format                  = input.resource->GetDesc().Format;
                        srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        srv.Texture2D.MipLevels     = 1;
                        auto slot                   = cpu;
                        slot.ptr += b * srvStride;
                        device->CreateShaderResourceView(input.resource.Get(), &srv, slot);
                    }
                    commands->SetGraphicsRootConstantBufferView(0, uniforms->GetGPUVirtualAddress() + index * 256);
                    commands->SetGraphicsRootDescriptorTable(1, gpu);
                    D3D12_VIEWPORT vp{0, 0, float(target.width), float(target.height), 0, 1};
                    D3D12_RECT     scissor{0, 0, LONG(target.width), LONG(target.height)};
                    commands->RSSetViewports(1, &vp);
                    commands->RSSetScissorRects(1, &scissor);
                    commands->DrawInstanced(scene ? 79 * 6 : 3, 1, 0, 0);
                    if (p.multisampled)
                        for (int a = 0; a < 2; a++)
                        {
                            Transition(textures[AAMultiColor + a], D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                            Transition(textures[p.target + a], D3D12_RESOURCE_STATE_RESOLVE_DEST);
                            commands->ResolveSubresource(
                              textures[p.target + a].resource.Get(),
                              0,
                              textures[AAMultiColor + a].resource.Get(),
                              0,
                              DXGI_FORMAT_R16G16B16A16_FLOAT);
                        }
                }
                Transition(textures[AAOutput], D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION from{};
                from.pResource = textures[AAOutput].resource.Get();
                from.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};
                to.pResource       = readback.Get();
                to.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                to.PlacedFootprint = footprint;
                commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
                End();
                D3D12_RANGE range{size_t(footprint.Offset), size_t(readbackSize)};
                Check(readback->Map(0, &range, &mapped));
                auto image = AAReadImage(
                  reinterpret_cast<uint16_t*>(static_cast<char*>(mapped) + footprint.Offset),
                  width,
                  height,
                  footprint.Footprint.RowPitch);
                D3D12_RANGE written{0, 0};
                readback->Unmap(0, &written);
                return image;
            }
        };
    } // namespace
    std::unique_ptr<AADevice> CreateD3D12AA(const RendererConfig& c)
    {
        return std::make_unique<D3D12AA>(c);
    }
} // namespace render::detail
