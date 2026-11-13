#include "../Backend.hpp"
#include "../ShaderData.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <climits>
#include <cstring>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace render::detail
{
    using Microsoft::WRL::ComPtr;
    static void checkHR(HRESULT hr, const char* operation)
    {
        if (FAILED(hr))
            throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(hr));
    }
    class D3D12Backend final : public Backend
    {
      public:
        ~D3D12Backend() override
        {
            if (queue && fence && event)
            {
                try
                {
                    WaitIdle();
                }
                catch (const std::exception& e)
                {
                    std::cerr << e.what() << '\n';
                }
            }
            if (event)
                CloseHandle(event);
        }
        void Initialize(GLFWwindow* window, const RendererConfig& config)
        {
            settings = config;
            hwnd     = glfwGetWin32Window(window);
            if (!hwnd)
                throw std::runtime_error("GLFW window has no HWND");
            if (config.enableValidation)
            {
                ComPtr<ID3D12Debug> debug;
                if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
                {
                    debug->EnableDebugLayer();
                    std::cout << "D3D12 debug layer: enabled\n";
                }
                else
                    std::cerr << "D3D12 debug layer unavailable (install Windows Graphics Tools).\n";
            }
            checkHR(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Create DXGI factory");
            ComPtr<IDXGIAdapter1> adapter;
            if (config.useWarp)
            {
                checkHR(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "Find WARP adapter");
            }
            else
            {
                for (UINT i = 0;; ++i)
                {
                    ComPtr<IDXGIAdapter1> candidate;
                    const auto            result = factory->EnumAdapters1(i, &candidate);
                    if (result == DXGI_ERROR_NOT_FOUND)
                        break;
                    checkHR(result, "Enumerate adapters");
                    DXGI_ADAPTER_DESC1 desc{};
                    checkHR(candidate->GetDesc1(&desc), "Describe adapter");
                    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                        continue;
                    const auto supported =
                      D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
                    std::wcout << L"Adapter: " << desc.Description << L"; D3D12 supported: "
                               << (SUCCEEDED(supported) ? L"yes" : L"no") << L'\n';
                    if (SUCCEEDED(supported))
                    {
                        adapter = candidate;
                        break;
                    }
                }
                if (!adapter)
                    throw std::runtime_error(
                      "No hardware/virtual adapter supports D3D12; use --api dx12 --warp for CPU validation");
            }
            checkHR(
              D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "Create D3D12 device");
            DXGI_ADAPTER_DESC1 selected{};
            checkHR(adapter->GetDesc1(&selected), "Describe selected adapter");
            std::wcout << L"D3D12 device: " << selected.Description
                       << (config.useWarp ? L" (WARP / CPU)" : L" (hardware / virtual GPU)") << L'\n';
            D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_0};
            if (
              FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))
              || shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0)
                throw std::runtime_error(
                  "DXC shaders require Shader Model 6.0 / DXIL; selected adapter does not support it. "
                  "Use --api dx12 --warp for CPU validation or a DXIL-capable GPU.");
            std::cout << "Shader Model 6.0 / DXIL: supported\n";
            if (config.enableValidation)
                device.As(&infoQueue);
            D3D12_COMMAND_QUEUE_DESC queueDesc{};
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            checkHR(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "Create command queue");
            checkHR(
              device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
              "Create allocator");
            checkHR(
              device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)),
              "Create command list");
            checkHR(commands->Close(), "Close initial command list");
            checkHR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create fence");
            event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!event)
                throw std::runtime_error("Create fence event failed");
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heap.NumDescriptors = BufferCount;
            checkHR(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvHeap)), "Create RTV heap");
            rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            CreatePipeline(config.shaderDirectory);
        }
        void Resize(uint32_t newWidth, uint32_t newHeight) override
        {
            if (width == newWidth && height == newHeight)
                return;
            width   = newWidth;
            height  = newHeight;
            resized = true;
        }
        FrameResult DrawFrame() override
        {
            if (!width || !height)
                return FrameResult::Skipped;
            WaitIdle();
            if (model && gpuModel != model.get())
                UploadModel();
            if (!swapchain || resized)
                Recreate();
            if (model && !modelDepth)
                ModelDepth();
            if (model)
            {
                const auto  u = MakeSceneUniforms(*model, camera, float(width) / height, lighting, directionalLight);
                void*       mapped = nullptr;
                D3D12_RANGE read{0, 0};
                checkHR(modelUniform->Map(0, &read, &mapped), "Map scene uniforms");
                std::memcpy(mapped, &u, sizeof(u));
                modelUniform->Unmap(0, nullptr);
            }
            const UINT index = swapchain->GetCurrentBackBufferIndex();
            checkHR(allocator->Reset(), "Reset allocator");
            checkHR(
              commands->Reset(allocator.Get(), model ? modelPipeline.Get() : pipeline.Get()), "Reset command list");
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = targets[index].Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            commands->ResourceBarrier(1, &barrier);
            auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtv.ptr += static_cast<SIZE_T>(index) * rtvStride;
            const float clear[]{0.025f, 0.035f, 0.055f, 1.0f};
            commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
            D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
            if (model)
            {
                dsv = modelDepthHeap->GetCPUDescriptorHandleForHeapStart();
                commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
            }
            commands->OMSetRenderTargets(1, &rtv, FALSE, model ? &dsv : nullptr);
            commands->SetGraphicsRootSignature(model ? modelRoot.Get() : root.Get());
            if (model)
            {
                ID3D12DescriptorHeap* heaps[] = {modelHeap.Get()};
                commands->SetDescriptorHeaps(1, heaps);
                commands->SetGraphicsRootConstantBufferView(0, modelUniform->GetGPUVirtualAddress());
                commands->SetGraphicsRootShaderResourceView(1, modelVertices->GetGPUVirtualAddress());
                commands->SetGraphicsRootDescriptorTable(2, modelHeap->GetGPUDescriptorHandleForHeapStart());
            }
            D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
            D3D12_RECT     scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
            commands->RSSetViewports(1, &viewport);
            commands->RSSetScissorRects(1, &scissor);
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commands->DrawInstanced(model ? static_cast<UINT>(model->vertices.size()) : 3, 1, 0, 0);
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            commands->ResourceBarrier(1, &barrier);
            checkHR(commands->Close(), "Close commands");
            ID3D12CommandList* lists[]{commands.Get()};
            queue->ExecuteCommandLists(1, lists);
            checkHR(swapchain->Present(1, 0), "Present");
            WaitIdle(); // Keep one frame in flight and inspect debug messages before reporting success.
            return FrameResult::Rendered;
        }
        void WaitIdle() override
        {
            const auto value = ++fenceValue;
            checkHR(queue->Signal(fence.Get(), value), "Signal fence");
            if (fence->GetCompletedValue() < value)
            {
                checkHR(fence->SetEventOnCompletion(value, event), "Set fence event");
                if (WaitForSingleObject(event, INFINITE) != WAIT_OBJECT_0)
                    throw std::runtime_error("Wait for D3D12 fence failed");
            }
            checkHR(device->GetDeviceRemovedReason(), "D3D12 device status");
            CollectMessages();
        }
        uint32_t ValidationErrorCount() const override { return errors; }

      private:
        static constexpr UINT             BufferCount = 2;
        HWND                              hwnd        = nullptr;
        HANDLE                            event       = nullptr;
        uint32_t                          width = 0, height = 0, errors = 0;
        UINT                              rtvStride  = 0;
        UINT64                            fenceValue = 0;
        bool                              resized    = true;
        ComPtr<IDXGIFactory4>             factory;
        ComPtr<ID3D12Device>              device;
        ComPtr<ID3D12InfoQueue>           infoQueue;
        ComPtr<ID3D12CommandQueue>        queue;
        ComPtr<IDXGISwapChain3>           swapchain;
        ComPtr<ID3D12DescriptorHeap>      rtvHeap;
        ComPtr<ID3D12Resource>            targets[BufferCount];
        ComPtr<ID3D12CommandAllocator>    allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        ComPtr<ID3D12Fence>               fence;
        ComPtr<ID3D12RootSignature>       root;
        ComPtr<ID3D12PipelineState>       pipeline;

#include "D3D12Model.inc"

        void CollectMessages()
        {
            if (!infoQueue)
                return;
            const auto count = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (UINT64 i = 0; i < count; ++i)
            {
                SIZE_T size = 0;
                checkHR(infoQueue->GetMessage(i, nullptr, &size), "Get debug message size");
                std::vector<char> storage(size);
                auto*             message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                checkHR(infoQueue->GetMessage(i, message, &size), "Get debug message");
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                    std::cerr << "[D3D12] " << message->pDescription << '\n';
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                    ++errors;
            }
            infoQueue->ClearStoredMessages();
        }
        void Recreate()
        {
            // Reset the closed command list to drop references to old back buffers.
            checkHR(allocator->Reset(), "Reset resize allocator");
            checkHR(commands->Reset(allocator.Get(), nullptr), "Reset resize commands");
            checkHR(commands->Close(), "Close resize commands");
            for (auto& target : targets)
                target.Reset();
            if (swapchain)
            {
                checkHR(
                  swapchain->ResizeBuffers(BufferCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0),
                  "Resize swapchain");
            }
            else
            {
                DXGI_SWAP_CHAIN_DESC1 desc{};
                desc.Width            = width;
                desc.Height           = height;
                desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount      = BufferCount;
                desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                ComPtr<IDXGISwapChain1> created;
                checkHR(
                  factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &desc, nullptr, nullptr, &created),
                  "Create swapchain");
                checkHR(created.As(&swapchain), "Query swapchain");
                checkHR(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "Disable DXGI Alt+Enter");
            }
            auto                          rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_RENDER_TARGET_VIEW_DESC view{};
            view.Format        = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            for (UINT i = 0; i < BufferCount; ++i)
            {
                checkHR(swapchain->GetBuffer(i, IID_PPV_ARGS(&targets[i])), "Get back buffer");
                device->CreateRenderTargetView(targets[i].Get(), &view, rtv);
                rtv.ptr += rtvStride;
            }
            modelDepth.Reset();
            resized = false;
            std::cout << "D3D12 swapchain: " << width << 'x' << height << '\n';
        }
        void CreatePipeline(const std::filesystem::path& shaderDirectory)
        {
            D3D12_ROOT_SIGNATURE_DESC desc{};
            desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            ComPtr<ID3DBlob> signature, messages;
            checkHR(
              D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &messages),
              "Serialize root signature");
            checkHR(
              device->CreateRootSignature(
                0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)),
              "Create root signature");
            auto                               vert = LoadShader(shaderDirectory, "triangle.vert.dxil");
            auto                               frag = LoadShader(shaderDirectory, "triangle.frag.dxil");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC info{};
            info.pRootSignature                     = root.Get();
            info.VS                                 = {vert.data(), vert.size()};
            info.PS                                 = {frag.data(), frag.size()};
            info.RasterizerState.FillMode           = D3D12_FILL_MODE_SOLID;
            info.RasterizerState.CullMode           = D3D12_CULL_MODE_NONE;
            info.RasterizerState.DepthClipEnable    = TRUE;
            auto& blend                             = info.BlendState.RenderTarget[0];
            blend.SrcBlend                          = D3D12_BLEND_ONE;
            blend.DestBlend                         = D3D12_BLEND_ZERO;
            blend.BlendOp                           = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha                     = D3D12_BLEND_ONE;
            blend.DestBlendAlpha                    = D3D12_BLEND_ZERO;
            blend.BlendOpAlpha                      = D3D12_BLEND_OP_ADD;
            blend.LogicOp                           = D3D12_LOGIC_OP_NOOP;
            blend.RenderTargetWriteMask             = D3D12_COLOR_WRITE_ENABLE_ALL;
            info.DepthStencilState.DepthEnable      = FALSE;
            info.DepthStencilState.DepthWriteMask   = D3D12_DEPTH_WRITE_MASK_ZERO;
            info.DepthStencilState.DepthFunc        = D3D12_COMPARISON_FUNC_ALWAYS;
            info.DepthStencilState.StencilEnable    = FALSE;
            info.DepthStencilState.StencilReadMask  = D3D12_DEFAULT_STENCIL_READ_MASK;
            info.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
            info.DepthStencilState.FrontFace        = {
              D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
            info.DepthStencilState.BackFace = info.DepthStencilState.FrontFace;
            info.SampleMask                 = UINT_MAX;
            info.SampleDesc.Count           = 1;
            info.PrimitiveTopologyType      = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            info.NumRenderTargets           = 1;
            info.RTVFormats[0]              = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            checkHR(device->CreateGraphicsPipelineState(&info, IID_PPV_ARGS(&pipeline)), "Create graphics pipeline");
        }
    };
    std::unique_ptr<Backend> CreateD3D12Backend(GLFWwindow* window, const RendererConfig& config)
    {
        auto backend = std::make_unique<D3D12Backend>();
        backend->Initialize(window, config);
        return backend;
    }
} // namespace render::detail
