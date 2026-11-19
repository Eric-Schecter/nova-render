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
            AAExternalScene external;
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
            ComPtr<ID3D12RootSignature>        root, computeRoot;
            ComPtr<ID3D12DescriptorHeap>       rtvHeap, dsvHeap, srvHeap;
            ComPtr<ID3D12PipelineState>        pipelines[30], sceneMS, shadowMS, hdrMS, ssrMS;
            ComPtr<ID3D12Resource>             uniforms, readback;
            Texture                            textures[AAResourceCount], depth, depthMS, depthBaseline, depthShadow;
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
            ComPtr<ID3D12PipelineState> ComputePipeline(int program, const RendererConfig& c)
            {
                auto data = LoadShader(c.shaderDirectory, ("aa-" + std::to_string(program) + ".comp.dxil").c_str());
                D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
                d.pRootSignature = computeRoot.Get();
                d.CS             = {data.data(), data.size()};
                ComPtr<ID3D12PipelineState> result;
                Check(device->CreateComputePipelineState(&d, IID_PPV_ARGS(&result)));
                return result;
            }
            void Dispatch(const AAPass& p, size_t index)
            {
                auto& target = textures[p.target];
                Transition(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
                commands->SetDescriptorHeaps(1, heaps);
                auto cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
                cpu.ptr += index * 18 * srvStride;
                auto gpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
                gpu.ptr += index * 18 * srvStride;
                for (int i = 0; i < 2; i++)
                {
                    auto& input = textures[p.sources[4 + i]];
                    Transition(input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                    srv.Format                  = input.resource->GetDesc().Format;
                    srv.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srv.Texture2D.MipLevels     = 1;
                    auto slot                   = cpu;
                    slot.ptr += i * srvStride;
                    device->CreateShaderResourceView(input.resource.Get(), &srv, slot);
                }
                D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                uav.Format        = DXGI_FORMAT_R32G32B32A32_FLOAT;
                uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                auto uavCPU       = cpu;
                uavCPU.ptr += 2 * srvStride;
                device->CreateUnorderedAccessView(target.resource.Get(), nullptr, &uav, uavCPU);
                auto uavGPU = gpu;
                uavGPU.ptr += 2 * srvStride;
                commands->SetComputeRootSignature(computeRoot.Get());
                commands->SetPipelineState(pipelines[p.program].Get());
                commands->SetComputeRootConstantBufferView(0, uniforms->GetGPUVirtualAddress() + index * 256);
                commands->SetComputeRootDescriptorTable(1, gpu);
                commands->SetComputeRootDescriptorTable(2, uavGPU);
                commands->Dispatch(16, 9, p.program == 25 && p.uniforms[0][2] > 2.5f ? 16 : 1);
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
                const bool isDepth               = format == DXGI_FORMAT_D32_FLOAT;
                d.Flags                          = lookup    ? D3D12_RESOURCE_FLAG_NONE
                                                   : isDepth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                                                             : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                if (format == DXGI_FORMAT_R32G32B32A32_FLOAT)
                    d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                D3D12_CLEAR_VALUE clear{};
                clear.Format = format;
                if (isDepth)
                    clear.DepthStencil.Depth = 1;
                Check(device->CreateCommittedResource(
                  &heap,
                  D3D12_HEAP_FLAG_NONE,
                  &d,
                  D3D12_RESOURCE_STATE_COMMON,
                  lookup || !isDepth ? nullptr : &clear,
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
                ComPtr<ID3D12InfoQueue> messages;
                if (SUCCEEDED(device.As(&messages)))
                {
                    const auto count = messages->GetNumStoredMessagesAllowedByRetrievalFilter();
                    for (UINT64 i = 0; i < count; i++)
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
                d.NumRenderTargets      = (program == 17 || program == 22)                   ? 4
                                          : (program == 0 || program == 10 || program == 16) ? 2
                                                                                             : 1;
                for (UINT i = 0; i < d.NumRenderTargets; i++)
                {
                    d.RTVFormats[i] = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    auto& blend     = d.BlendState.RenderTarget[i];
                    blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
                    blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
                    blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                    blend.LogicOp                      = D3D12_LOGIC_OP_NOOP;
                    blend.RenderTargetWriteMask        = D3D12_COLOR_WRITE_ENABLE_ALL;
                }
                d.RasterizerState.FillMode          = D3D12_FILL_MODE_SOLID;
                d.RasterizerState.CullMode          = D3D12_CULL_MODE_NONE;
                d.RasterizerState.DepthClipEnable   = TRUE;
                d.RasterizerState.MultisampleEnable = samples > 1;
                d.DepthStencilState.DepthEnable     = program == 0 || program == 9 || program == 10 || program == 16
                                                  || program == 17 || program == 22 || program == 23 || program == 27;
                d.DepthStencilState.DepthWriteMask =
                  (program == 0 || program == 9 || program == 10 || program == 16 || program == 17 || program == 22
                   || program == 23 || program == 27)
                    ? D3D12_DEPTH_WRITE_MASK_ALL
                    : D3D12_DEPTH_WRITE_MASK_ZERO;
                d.DepthStencilState.DepthFunc       = D3D12_COMPARISON_FUNC_LESS;
                d.DepthStencilState.StencilReadMask = d.DepthStencilState.StencilWriteMask = 255;
                d.DepthStencilState.FrontFace.StencilFailOp   = d.DepthStencilState.FrontFace.StencilDepthFailOp =
                  d.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
                d.DepthStencilState.FrontFace.StencilFunc     = D3D12_COMPARISON_FUNC_ALWAYS;
                d.DepthStencilState.BackFace                  = d.DepthStencilState.FrontFace;
                d.DSVFormat = (program == 0 || program == 9 || program == 10 || program == 16 || program == 17
                               || program == 22 || program == 23 || program == 27)
                                ? DXGI_FORMAT_D32_FLOAT
                                : DXGI_FORMAT_UNKNOWN;
                ComPtr<ID3D12PipelineState> result;
                Check(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&result)));
                return result;
            }

          public:
            explicit D3D12AA(const RendererConfig& c, const AAExternalScene& borrowed)
              : external(borrowed)
            {
                ConfigureD3D12DebugLayer(c.enableValidation);
                if (!external.device)
                {
                    ComPtr<IDXGIFactory4> factory;
                    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
                    ComPtr<IDXGIAdapter> adapter;
                    if (c.useWarp)
                        Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
                    Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
                }
                else
                    device = static_cast<ID3D12Device*>(external.device);
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
                range.NumDescriptors                    = 18;
                range.BaseShaderRegister                = 1;
                range.OffsetInDescriptorsFromTableStart = 0;
                D3D12_ROOT_PARAMETER parameters[2]{};
                parameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
                parameters[0].Descriptor.ShaderRegister = 0;
                parameters[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
                parameters[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameters[1].DescriptorTable           = {1, &range};
                parameters[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
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
                D3D12_DESCRIPTOR_RANGE cr[2]{};
                cr[0].RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                cr[0].NumDescriptors     = 2;
                cr[0].BaseShaderRegister = 5;
                cr[1].RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                cr[1].NumDescriptors     = 1;
                cr[1].BaseShaderRegister = 19;
                D3D12_ROOT_PARAMETER cp[3]{};
                cp[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
                cp[0].Descriptor.ShaderRegister = 0;
                for (int i = 1; i < 3; i++)
                {
                    cp[i].ParameterType   = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    cp[i].DescriptorTable = {1, &cr[i - 1]};
                }
                D3D12_ROOT_SIGNATURE_DESC cs{3, cp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
                data.Reset();
                error.Reset();
                Check(D3D12SerializeRootSignature(&cs, D3D_ROOT_SIGNATURE_VERSION_1, &data, &error));
                Check(device->CreateRootSignature(
                  0, data->GetBufferPointer(), data->GetBufferSize(), IID_PPV_ARGS(&computeRoot)));
                D3D12_DESCRIPTOR_HEAP_DESC heap{};
                heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                heap.NumDescriptors = AAResourceCount;
                Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvHeap)));
                heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                heap.NumDescriptors = 4;
                Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&dsvHeap)));
                heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                heap.NumDescriptors = 64 * 18;
                heap.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srvHeap)));
                rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                dsvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
                srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                uniforms  = Buffer(256 * 64, D3D12_HEAP_TYPE_UPLOAD);
                for (int i = 0; i < 30; i++)
                    pipelines[i] = (i == 25 || i == 26) ? ComputePipeline(i, c) : Pipeline(i, 1, c);
                sceneMS  = Pipeline(0, 4, c);
                shadowMS = Pipeline(10, 4, c);
                hdrMS    = Pipeline(16, 4, c);
                ssrMS    = Pipeline(17, 4, c);
                for (int i = AAArea; i <= AASearch; i++)
                {
                    const auto table = AALookup(i == AAArea);
                    textures[i]      = MakeTexture(table.width, table.height, DXGI_FORMAT_R8G8B8A8_UNORM, 1, true);
                    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
                    UINT64                             bytes;
                    auto                               desc = textures[i].resource->GetDesc();
                    device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
                    auto upload = Buffer(bytes, D3D12_HEAP_TYPE_UPLOAD);

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
            void SetReflectionEnvironment(const AATextureData& data, int slot) override
            {
                auto next = MakeTexture(data.width, data.height, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, true);
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
                UINT64                             bytes;
                auto                               desc = next.resource->GetDesc();
                device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
                auto        upload = Buffer(bytes, D3D12_HEAP_TYPE_UPLOAD);
                void*       mapped;
                D3D12_RANGE empty{0, 0};
                Check(upload->Map(0, &empty, &mapped));
                for (uint32_t y = 0; y < data.height; y++)
                    std::memcpy(
                      static_cast<char*>(mapped) + fp.Offset + y * fp.Footprint.RowPitch,
                      data.pixels.data() + size_t(y) * data.width * 8,
                      data.width * 8);
                upload->Unmap(0, nullptr);
                Begin();
                Transition(next, D3D12_RESOURCE_STATE_COPY_DEST);
                D3D12_TEXTURE_COPY_LOCATION to{};
                to.pResource = next.resource.Get();
                to.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION from{};
                from.pResource       = upload.Get();
                from.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                from.PlacedFootprint = fp;
                commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
                Transition(next, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                End();
                textures[slot] = std::move(next);
            }
            void Resize(uint32_t w, uint32_t h, bool ss) override
            {
                width        = w;
                height       = h;
                supersampled = ss;
            }
            void Ensure(const std::vector<AAPass>& passes)
            {
                const auto used    = AAUsed(passes);
                bool       changed = false;
                for (int i = 0; i < AAResourceCount; i++)
                    if (i != AAArea && i != AASearch && i != AASsrEnvironment && i != AAProbeData)
                    {
                        if (!used[i])
                        {
                            textures[i] = {};
                            continue;
                        }
                        const auto spec = AAResourceSpec(
                          i,
                          width,
                          height,
                          supersampled,
                          bool(external.draw),
                          uint32_t(passes.front().uniforms[8][1]),
                          passes.front().uniforms[6][3] > .5f);
                        if (
                          !textures[i].resource || textures[i].width != spec.width || textures[i].height != spec.height)
                        {
                            textures[i] = MakeTexture(
                              spec.width,
                              spec.height,
                              (i == AAPipelineTiles || i == AAPipelineLists) ? DXGI_FORMAT_R32G32B32A32_FLOAT
                                                                             : DXGI_FORMAT_R16G16B16A16_FLOAT,
                              spec.samples);
                            device->CreateRenderTargetView(textures[i].resource.Get(), nullptr, RTV(i));
                            if (i == AAOutput)
                                changed = true;
                        }
                    }
                bool needed[4]{};
                for (const auto& p : passes)
                    if (((p.program <= 0 && p.program != -8) || p.program == 9 || p.program == 10 || p.program == 16 || p.program == 17
                         || p.program == 22 || p.program == 23 || p.program == 27))
                    {
                        int k         = p.program == 9                                          ? 3
                                        : (p.target == AABaseline || p.target == AARawBaseline) ? 2
                                        : p.multisampled                                        ? 1
                                                                                                : 0;
                        needed[k]     = true;
                        auto&       d = k == 3 ? depthShadow : k == 2 ? depthBaseline : k == 1 ? depthMS : depth;
                        const auto& t = textures[p.target];
                        if (!d.resource || d.width != t.width || d.height != t.height)
                        {
                            d = MakeTexture(t.width, t.height, DXGI_FORMAT_D32_FLOAT, k == 1 ? 4 : 1);
                            device->CreateDepthStencilView(d.resource.Get(), nullptr, DSV(k));
                        }
                    }
                if (!needed[3])
                    depthShadow = {};
                if (!needed[0])
                    depth = {};
                if (!needed[1])
                    depthMS = {};
                if (!needed[2])
                    depthBaseline = {};
                if (changed)
                {
                    auto desc = textures[AAOutput].resource->GetDesc();
                    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &readbackSize);
                    readback = Buffer(readbackSize, D3D12_HEAP_TYPE_READBACK);
                }
            }
            void* GPUOutput() const override { return textures[AAOutput].resource.Get(); }

            Image Capture() override
            {
                if (!textures[AAOutput].resource)
                    throw std::runtime_error("Draw before GPU capture");
                Begin();
                Transition(textures[AAOutput], D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION from{};
                from.pResource = textures[AAOutput].resource.Get();
                from.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION to{};
                to.pResource       = readback.Get();
                to.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                to.PlacedFootprint = footprint;
                commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
                Transition(textures[AAOutput], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                End();
                void*       mapped;
                D3D12_RANGE range{size_t(footprint.Offset), size_t(readbackSize)};
                Check(readback->Map(0, &range, &mapped));
                auto image = AAReadImage(
                  reinterpret_cast<uint16_t*>(static_cast<char*>(mapped) + footprint.Offset),
                  width,
                  height,
                  footprint.Footprint.RowPitch);
                D3D12_RANGE written{0, 0};
                readback->Unmap(0, &written);
                readbackCount++;
                return image;
            }
            Image Draw(const std::vector<AAPass>& passes, bool readbackImage = true) override
            {
                Ensure(passes);
                void*       mapped;
                D3D12_RANGE empty{0, 0};
                Check(uniforms->Map(0, &empty, &mapped));
                for (size_t i = 0; i < passes.size(); i++)
                    std::memcpy(static_cast<char*>(mapped) + i * 256, passes[i].uniforms.data(), sizeof(AAUniforms));
                uniforms->Unmap(0, nullptr);
                Begin();
                for (int i : {AAHistoryA, AAHistoryB, AAPrevMotion, AADummy})
                    if (textures[i].resource && (passes.front().uniforms[3][0] < .5f || i == AADummy))
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
                    const auto& p = passes[index];
                    if (p.program == 25 || p.program == 26)
                    {
                        Dispatch(p, index);
                        continue;
                    }
                    const bool  scene =
                      ((p.program <= 0 && p.program != -8) || p.program == 9 || p.program == 10 || p.program == 16 || p.program == 17
                       || p.program == 22 || p.program == 23 || p.program == 27);
                    const bool                  nativeScene = p.program < 0;
                    const int                   attachments = p.program == -4 ? 5
                                                              : (p.program == 17 || p.program == 22 || p.program == -7) ? 4
                                                              : (p.program == 0 || p.program == 10 || p.program == 16 || p.program == -6)
                                                                ? 2
                                                                : 1;
                    auto&                       target      = textures[p.target];
                    D3D12_CPU_DESCRIPTOR_HANDLE targets[5];
                    for (int a = 0; a < attachments; a++)
                    {
                        const int n = p.multisampled ? (p.program == 17 ? AASsrMulti0
                                                        : nativeScene   ? AARawMultiColor
                                                                        : AAMultiColor)
                                                         + a
                                      : p.program == -4 && a == 4 ? AAModelClearcoat
                                                                  : p.target + a;
                        Transition(textures[n], D3D12_RESOURCE_STATE_RENDER_TARGET);
                        targets[a]         = RTV(n);
                        float colors[5][4] = {{0, 0, 0, 0}, {0, 0, 1, 1}};
                        std::copy(p.uniforms[7].begin(), p.uniforms[7].end(), colors[0]);
                        if (p.program == -2 || p.program == -3)
                        {
                            colors[0][0] = colors[0][1] = 0;
                            colors[0][2] = colors[0][3] = 1;
                        }
                        if (p.program == 17 && a == 2)
                        {
                            colors[a][0] = colors[a][1] = colors[a][2] = 0;
                            colors[a][3]                               = 1;
                        }
                        if (p.program == 17 && a == 3)
                        {
                            colors[a][0] = colors[a][1] = 1000;
                            colors[a][2]                = 0;
                            colors[a][3]                = 1;
                        }
                        if ((p.program >= 22 && p.program != 23) || p.program == -4 || p.program == -7)
                            std::fill(std::begin(colors[a]), std::end(colors[a]), 0.f);
                        if (p.program == -6)
                            std::fill(std::begin(colors[a]), std::end(colors[a]), a == 0 ? 0.f : 1.f);
                        commands->ClearRenderTargetView(targets[a], colors[a], 0, nullptr);
                    }
                    auto dsv = DSV(
                      p.program == 9                                          ? 3
                      : (p.target == AABaseline || p.target == AARawBaseline) ? 2
                      : p.multisampled                                        ? 1
                                                                              : 0);
                    if (scene)
                    {
                        auto& d = p.program == 9                                          ? depthShadow
                                  : (p.target == AABaseline || p.target == AARawBaseline) ? depthBaseline
                                  : p.multisampled                                        ? depthMS
                                                                                          : depth;
                        Transition(d, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                        if (p.program != -5 && p.program != -6 && p.program != -7)
                            commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
                    }
                    commands->OMSetRenderTargets(attachments, targets, FALSE, scene ? &dsv : nullptr);
                    if (nativeScene)
                    {
                        D3D12_VIEWPORT viewport{
                          1 + p.uniforms[1][0],
                          1 + p.uniforms[1][1],
                          float(target.width - 2),
                          float(target.height - 2),
                          0,
                          1};
                        D3D12_RECT scissor{0, 0, LONG(target.width), LONG(target.height)};
                        commands->RSSetViewports(1, &viewport);
                        commands->RSSetScissorRects(1, &scissor);
                        if (p.program == -5 || p.program == -7 || p.program == -8)
                            for (int i = 0; i < 10; i++)
                                Transition(textures[p.sources[i]], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                        if ((p.program == -5 || p.program == -7 || p.program == -8) && external.bindGBuffer)
                        {
                            std::array<void*, 10> views{};
                            for (size_t i = 0; i < views.size(); i++)
                                views[i] = textures[p.sources[i]].resource.Get();
                            external.bindGBuffer(commands.Get(), views, p.program);
                        }
                        if (p.program != -3)
                            external.draw(p, commands.Get(), nullptr);
                    }
                    else
                    {
                        ID3D12DescriptorHeap* aaHeaps[] = {srvHeap.Get()};
                        commands->SetDescriptorHeaps(1, aaHeaps);
                        commands->SetGraphicsRootSignature(root.Get());
                        commands->SetPipelineState(
                          p.multisampled ? (p.program == 17   ? ssrMS.Get()
                                            : p.program == 16 ? hdrMS.Get()
                                            : p.program == 10 ? shadowMS.Get()
                                                              : sceneMS.Get())
                                         : pipelines[p.program].Get());
                        auto cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
                        cpu.ptr += index * 18 * srvStride;
                        auto gpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
                        gpu.ptr += index * 18 * srvStride;
                        for (int b = 0; b < 18; b++)
                        {
                            auto& input = textures[(b == 8 || b == 9) ? AADummy : p.sources[b < 8 ? b : b - 2]];
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
                        commands->DrawInstanced(
                          (p.program == 9 || p.program == 10 || p.program == 16 || p.program == 17 || p.program == 22
                           || p.program == 23 || p.program == 27)
                            ? uint32_t(p.uniforms[10][2])
                          : scene ? 79 * 6
                                  : 3,
                          1,
                          0,
                          0);
                    }
                    if (p.multisampled)
                        for (int a = 0; a < attachments; a++)
                        {
                            Transition(
                              textures
                                [(p.program == 17 ? AASsrMulti0
                                  : nativeScene   ? AARawMultiColor
                                                  : AAMultiColor)
                                 + a],
                              D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                            Transition(textures[p.target + a], D3D12_RESOURCE_STATE_RESOLVE_DEST);
                            commands->ResolveSubresource(
                              textures[p.target + a].resource.Get(),
                              0,
                              textures
                                [(p.program == 17 ? AASsrMulti0
                                  : nativeScene   ? AARawMultiColor
                                                  : AAMultiColor)
                                 + a]
                                  .resource.Get(),
                              0,
                              DXGI_FORMAT_R16G16B16A16_FLOAT);
                        }
                }
                if (!readbackImage)
                {
                    Transition(textures[AAOutput], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    End();
                    return {width, height, {}};
                }
                readbackCount++;
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
    std::unique_ptr<AADevice> CreateD3D12AA(const RendererConfig& c, const AAExternalScene& borrowed)
    {
        return std::make_unique<D3D12AA>(c, borrowed);
    }
} // namespace render::detail
