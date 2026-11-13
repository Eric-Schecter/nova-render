#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>

struct GLFWwindow;

namespace render {
enum class GraphicsAPI { Auto, Vulkan, D3D12, Metal };
const char* GraphicsAPIName(GraphicsAPI api);

struct RendererConfig {
    GraphicsAPI api = GraphicsAPI::Auto;
    bool enableValidation = true;
    bool useWarp = false; // Explicit D3D12 software device for validation.
    // Empty uses embedded triangle shaders; nonempty explicitly overrides from disk.
    std::filesystem::path shaderDirectory;
};

// Pure selection policy: resolves Auto, rejects unavailable backends and invalid WARP use.
// Does not initialize a device or silently retry another API.
bool IsGraphicsAPIAvailable(GraphicsAPI api);
GraphicsAPI ResolveGraphicsAPI(const RendererConfig& config);

enum class FrameResult { Rendered, Skipped };

// GLFW must be initialized. The window must outlive this object.
// Use on the window's main thread; only one renderer at a time is supported.
// Auto selects Metal on macOS, D3D12 on Windows, Vulkan on Linux.
class Renderer {
public:
    Renderer(GLFWwindow* window, const RendererConfig& config);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Supply framebuffer pixels before the first frame and after size changes.
    // Zero dimensions suspend rendering. Rebuilds happen inside DrawFrame().
    void Resize(uint32_t framebufferWidth, uint32_t framebufferHeight);
    FrameResult DrawFrame();
    void WaitIdle();
    GraphicsAPI GetAPI() const;
    uint32_t ValidationErrorCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace render
