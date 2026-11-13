#pragma once
#include "render/Renderer.hpp"
#include "ModelData.hpp"

namespace render::detail {
class Backend {
public:
    virtual ~Backend() = default;
    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual FrameResult DrawFrame() = 0;
    virtual void WaitIdle() = 0;
    virtual void LoadModel(std::shared_ptr<const ModelData> data) { model = std::move(data); }
    void SetCamera(const OrbitCamera& value) { camera = value; }
protected:
    std::shared_ptr<const ModelData> model;
    OrbitCamera camera;
public:
    virtual uint32_t ValidationErrorCount() const = 0;
};
std::unique_ptr<Backend> CreateVulkanBackend(GLFWwindow*, const RendererConfig&);
#ifdef __APPLE__
std::unique_ptr<Backend> CreateMetalBackend(GLFWwindow*, const RendererConfig&);
#endif
#ifdef _WIN32
std::unique_ptr<Backend> CreateD3D12Backend(GLFWwindow*, const RendererConfig&);
#endif
} // namespace render::detail
