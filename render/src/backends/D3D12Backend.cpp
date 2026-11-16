#include "../Backend.hpp"
#include "../ShaderData.hpp"
#include "../path/D3D12Debug.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <climits>
#include <cstring>
#include <d3d12.h>
#ifdef NOVA_HAS_UI
#include <imgui_impl_dx12.h>
#endif
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
#ifdef NOVA_HAS_UI
            if (uiRenderer)
            {
                parameterPanel->Activate();
                ImGui_ImplDX12_Shutdown();
            }
#endif
            if (event)
                CloseHandle(event);
        }
        void Initialize(GLFWwindow* window, const RendererConfig& config)
        {
            settings = config;
            hwnd     = glfwGetWin32Window(window);
            if (!hwnd)
                throw std::runtime_error("GLFW window has no HWND");
            const bool debug = ConfigureD3D12DebugLayer(config.enableValidation);
            if (config.enableValidation)
                std::cout << "D3D12 debug layer: " << (debug ? "enabled" : "unavailable for this process") << '\n';
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
            if (model && (gpuModel != model || gpuEnvironment != environment))
                UploadModel();
            if (!swapchain || resized)
                Recreate();
            if (model && !modelDepth)
                ModelDepth();
#ifdef NOVA_HAS_UI
            if (panelEnabled)
            {
                parameterPanel->Activate();
                if (uiRenderer && uiHasDepth != bool(model))
                {
                    ImGui_ImplDX12_Shutdown();
                    uiRenderer = false;
                }
                if (!uiRenderer)
                {
                    D3D12_DESCRIPTOR_HEAP_DESC desc{};
                    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                    desc.NumDescriptors = 1;
                    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                    checkHR(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&uiHeap)), "Create UI heap");
                    ImGui_ImplDX12_InitInfo info{};
                    info.Device            = device.Get();
                    info.CommandQueue      = queue.Get();
                    info.NumFramesInFlight = 1;
                    info.RTVFormat         = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                    info.DSVFormat         = model ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
                    info.SrvDescriptorHeap = uiHeap.Get();
                    // ImGui 1.91.9 uses one SRV for its font atlas. No user textures in this panel.
                    info.SrvDescriptorAllocFn = [](
                                                  ImGui_ImplDX12_InitInfo*     i,
                                                  D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                                  D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
                        *cpu = i->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
                        *gpu = i->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
                    };
                    info.SrvDescriptorFreeFn =
                      [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};
                    if (!ImGui_ImplDX12_Init(&info))
                        throw std::runtime_error("Initialize DX12 UI failed");
                    uiRenderer = true;
                    uiHasDepth = bool(model);
                }
                parameterPanel->SetSRGBOutput(true);
                ImGui_ImplDX12_NewFrame();
                DrawParameterPanel();
            }
#endif
            if (model && model->animation)
            {
                void*       mapped;
                D3D12_RANGE empty{0, 0};
                checkHR(modelJoints->Map(0, &empty, &mapped), "Map joint matrices");
                std::memcpy(mapped, model->jointMatrices.data(), model->jointMatrices.size() * sizeof(JointMatrix));
                modelJoints->Unmap(0, nullptr);
            }
            if (model)
            {
                size_t bytes = std::max(size_t(192), scene.instances.size() * sizeof(InstanceData));
                if (!sceneInstanceBuffer || bytes > sceneInstanceBytes)
                {
                    sceneInstanceBuffer = uploadBuffer(bytes);
                    sceneInstanceBytes  = bytes;
                }
                if (scene.active && !scene.instances.empty())
                {
                    void*       mapped = nullptr;
                    D3D12_RANGE read{0, 0};
                    checkHR(sceneInstanceBuffer->Map(0, &read, &mapped), "Map instances");
                    std::memcpy(mapped, scene.instances.data(), scene.instances.size() * sizeof(InstanceData));
                    sceneInstanceBuffer->Unmap(0, nullptr);
                }
            }
            const auto  topInset    = SceneTopInset(height);
            const auto  sceneWidth  = width;
            const auto  sceneHeight = height - topInset;
            const auto* path        = DrawPathFrame(sceneWidth, sceneHeight);
            if (path)
                UploadPathImage(*path);
            if (model)
            {
                auto u = MakeSceneUniforms(
                  *model,
                  camera,
                  float(sceneWidth) / sceneHeight,
                  lighting,
                  directionalLight,
                  environment.get(),
                  environmentSettings);
                u.skyParams[2]     = path ? 1.0f : 0.0f;
                u.lightDirection[3] = scene.active ? 1.f : 0.f;
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
            const auto clear = ClearColorForTarget(true);
            commands->ClearRenderTargetView(rtv, clear.data(), 0, nullptr);
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
                commands->SetGraphicsRootShaderResourceView(3, modelSkin->GetGPUVirtualAddress());
                commands->SetGraphicsRootShaderResourceView(4, modelJoints->GetGPUVirtualAddress());
                commands->SetGraphicsRootShaderResourceView(5, sceneInstanceBuffer->GetGPUVirtualAddress());
                commands->SetGraphicsRootDescriptorTable(2, modelHeap->GetGPUDescriptorHandleForHeapStart());
            }
            D3D12_VIEWPORT viewport{0, float(topInset), float(sceneWidth), float(sceneHeight), 0, 1};
            D3D12_RECT     scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
            commands->RSSetViewports(1, &viewport);
            commands->RSSetScissorRects(1, &scissor);
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            if ((environment && environmentSettings.showBackground) || path)
            {
                commands->SetPipelineState(skyPipeline.Get());
                commands->DrawInstanced(3, 1, 0, 0);
                commands->SetPipelineState(modelPipeline.Get());
            }
            if (!path && scene.active)
            {
                for (const auto& draw : scene.draws)
                    if (draw.instanceCount)
                        commands->DrawInstanced(
                          draw.vertexCount, draw.instanceCount, draw.firstVertex, draw.firstInstance);
            }
            else if (!path && (!model || !model->vertices.empty()))
                commands->DrawInstanced(model ? static_cast<UINT>(model->vertices.size()) : 3, 1, 0, 0);
#ifdef NOVA_HAS_UI
            if (panelEnabled)
            {
                ID3D12DescriptorHeap* heaps[]{uiHeap.Get()};
                commands->SetDescriptorHeaps(1, heaps);
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands.Get());
            }
#endif
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
#ifdef NOVA_HAS_UI
        bool                         uiRenderer = false, uiHasDepth = false;
        ComPtr<ID3D12DescriptorHeap> uiHeap;
#endif
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
        ComPtr<ID3D12PipelineState> aaModel[2], aaSky[2], aaTrianglePipeline[2], aaMotion, aaSkyMotion;
        ComPtr<ID3D12Resource>      aaPreviousUniform, aaPreviousPalette;
        size_t                      aaPreviousPaletteBytes = 0;
        ComPtr<ID3D12PipelineState> AAPipeline(const char* file, bool sky, uint32_t samples, bool triangle = false)
        {
            auto vert = LoadShader(settings.shaderDirectory, (std::string(file) + ".vert.dxil").c_str()),
                 frag = LoadShader(settings.shaderDirectory, (std::string(file) + ".frag.dxil").c_str());
            D3D12_GRAPHICS_PIPELINE_STATE_DESC info{};
            info.pRootSignature                    = triangle ? root.Get() : modelRoot.Get();
            info.VS                                = {vert.data(), vert.size()};
            info.PS                                = {frag.data(), frag.size()};
            info.RasterizerState.FillMode          = D3D12_FILL_MODE_SOLID;
            info.RasterizerState.CullMode          = D3D12_CULL_MODE_NONE;
            info.RasterizerState.DepthClipEnable   = TRUE;
            info.RasterizerState.MultisampleEnable = samples > 1;
            auto& blend                            = info.BlendState.RenderTarget[0];
            blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
            blend.BlendOp = blend.BlendOpAlpha     = D3D12_BLEND_OP_ADD;
            blend.LogicOp                          = D3D12_LOGIC_OP_NOOP;
            blend.RenderTargetWriteMask            = D3D12_COLOR_WRITE_ENABLE_ALL;
            info.DepthStencilState.DepthEnable     = TRUE;
            info.DepthStencilState.DepthWriteMask  = sky ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
            info.DepthStencilState.DepthFunc       = sky ? D3D12_COMPARISON_FUNC_ALWAYS : D3D12_COMPARISON_FUNC_LESS;
            info.DepthStencilState.StencilReadMask = info.DepthStencilState.StencilWriteMask = 255;
            auto& face         = info.DepthStencilState.FrontFace;
            face.StencilFailOp = face.StencilDepthFailOp = face.StencilPassOp = D3D12_STENCIL_OP_KEEP;
            face.StencilFunc                                                  = D3D12_COMPARISON_FUNC_ALWAYS;
            info.DepthStencilState.BackFace                                   = face;
            info.SampleMask                                                   = UINT_MAX;
            info.PrimitiveTopologyType                                        = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            info.NumRenderTargets                                             = 1;
            info.RTVFormats[0]                                                = DXGI_FORMAT_R16G16B16A16_FLOAT;
            info.DSVFormat                                                    = DXGI_FORMAT_D32_FLOAT;
            info.SampleDesc.Count                                             = samples;
            ComPtr<ID3D12PipelineState> result;
            checkHR(device->CreateGraphicsPipelineState(&info, IID_PPV_ARGS(&result)), "Create raster AA pipeline");
            return result;
        }
        void CreateRasterAADevice() override
        {
            AAExternalScene external;
            external.device = device.Get();
            external.draw   = [this](const AAPass& pass, void* command, void*) {
                auto* cmd    = static_cast<ID3D12GraphicsCommandList*>(command);
                int   sample = pass.multisampled ? 1 : 0;
                if (aaTriangle)
                {
                    if (!aaTrianglePipeline[sample])
                        aaTrianglePipeline[sample] = AAPipeline("triangle", false, sample ? 4 : 1, true);
                    cmd->SetGraphicsRootSignature(root.Get());
                    cmd->SetPipelineState(aaTrianglePipeline[sample].Get());
                    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    cmd->DrawInstanced(3, 1, 0, 0);
                    return;
                }
                const auto& current  = rasterAA.current;
                const auto& previous = rasterAA.frames ? rasterAA.previous : current;
                const auto& palette  = rasterAA.frames ? rasterAA.previousJoints : model->jointMatrices;
                if (!aaPreviousUniform)
                    aaPreviousUniform = uploadBuffer(512);
                size_t bytes = std::max(size_t(64), palette.size() * sizeof(JointMatrix));
                if (bytes > aaPreviousPaletteBytes)
                {
                    aaPreviousPalette      = uploadBuffer(bytes);
                    aaPreviousPaletteBytes = bytes;
                }
                auto write = [&](ID3D12Resource* buffer, const void* data, size_t size) {
                    if (!size)
                        return;
                    void*       mapped;
                    D3D12_RANGE range{0, 0};
                    checkHR(buffer->Map(0, &range, &mapped), "Map raster AA input");
                    std::memcpy(mapped, data, size);
                    buffer->Unmap(0, nullptr);
                };
                write(modelUniform.Get(), &current, sizeof(current));
                write(aaPreviousUniform.Get(), &previous, sizeof(previous));
                write(aaPreviousPalette.Get(), palette.data(), palette.size() * sizeof(JointMatrix));
                ID3D12DescriptorHeap* heaps[] = {modelHeap.Get()};
                cmd->SetDescriptorHeaps(1, heaps);
                cmd->SetGraphicsRootSignature(modelRoot.Get());
                cmd->SetGraphicsRootConstantBufferView(0, modelUniform->GetGPUVirtualAddress());
                cmd->SetGraphicsRootShaderResourceView(1, modelVertices->GetGPUVirtualAddress());
                cmd->SetGraphicsRootDescriptorTable(2, modelHeap->GetGPUDescriptorHandleForHeapStart());
                cmd->SetGraphicsRootShaderResourceView(3, modelSkin->GetGPUVirtualAddress());
                cmd->SetGraphicsRootShaderResourceView(4, modelJoints->GetGPUVirtualAddress());
                cmd->SetGraphicsRootShaderResourceView(5, sceneInstanceBuffer->GetGPUVirtualAddress());
                cmd->SetGraphicsRootConstantBufferView(6, aaPreviousUniform->GetGPUVirtualAddress());
                cmd->SetGraphicsRootShaderResourceView(7, aaPreviousPalette->GetGPUVirtualAddress());
                cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                bool motion = pass.program == -2;
                if (environment && environmentSettings.showBackground)
                {
                    auto& pipeline = motion ? aaSkyMotion : aaSky[sample];
                    if (!pipeline)
                        pipeline = AAPipeline(motion ? "sky-motion" : "skybox", true, sample ? 4 : 1);
                    cmd->SetPipelineState(pipeline.Get());
                    cmd->DrawInstanced(3, 1, 0, 0);
                }
                if (!model->vertices.empty())
                {
                    auto& pipeline = motion ? aaMotion : aaModel[sample];
                    if (!pipeline)
                        pipeline = AAPipeline(motion ? "model-motion" : "model", false, sample ? 4 : 1);
                    cmd->SetPipelineState(pipeline.Get());
                    if (scene.active)
                    {
                        for (const auto& draw : scene.draws)
                            if (draw.instanceCount)
                                cmd->DrawInstanced(
                                  draw.vertexCount, draw.instanceCount, draw.firstVertex, draw.firstInstance);
                    }
                    else
                        cmd->DrawInstanced(UINT(model->vertices.size()), 1, 0, 0);
                }
            };
            rasterAA.device = CreateD3D12AA(settings, external);
        }


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
