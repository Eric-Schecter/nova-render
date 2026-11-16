# Renderer

```cpp
#include "render/Renderer.hpp"
```

`Renderer` owns GPU resources; the application owns the GLFW window and event loop. Use it on the window's main thread. Only one live renderer is currently supported; copying and moving are disabled.

## Lifetime

```cpp
render::RendererConfig config;
config.shaderDirectory = APP_SHADER_DIR;
render::Renderer renderer(window, config);
```

Initialize GLFW and create a `GLFW_NO_API` window first. Destroy Renderer before the window, then terminate GLFW. Invalid configuration, unavailable devices and loading failures throw exceptions. Some resources are created on the first draw.

## Methods

```cpp
void Resize(uint32_t framebufferWidth, uint32_t framebufferHeight);
FrameResult DrawFrame();
void WaitIdle();
void SetAntiAliasing(const AntiAliasingSettings& settings);
AntiAliasingSettings GetAntiAliasing() const;
uint32_t GetAntiAliasingHistoryFrames() const;
void LoadModel(const std::filesystem::path& glbPath);
void LoadEnvironment(const std::filesystem::path& environmentPath);
void SetEnvironment(const EnvironmentSettings& settings);
EnvironmentSettings GetEnvironment() const;
void SetCamera(const OrbitCamera& camera);
OrbitCamera GetCamera() const;
GraphicsAPI GetAPI() const;
uint32_t ValidationErrorCount() const;
```

- `Resize`: pass framebuffer pixel dimensions before the first draw and after resizing. Zero dimensions pause rendering; reconstruction happens during a draw.
- `DrawFrame`: renders the built-in triangle or loaded model, returning `Rendered` or `Skipped`. It does not process window events and may throw.
- `WaitIdle`: waits for submitted work. Call before exit to catch deferred GPU errors.
- `GetAPI`: returns the selected concrete backend.
- `ValidationErrorCount`: reads accumulated backend errors. Wait for the GPU first; zero errors do not prove visual correctness or that validation was enabled.

`SetLighting(Unlit / PBR)` defaults to Unlit. `SetDirectionalLight` / `GetDirectionalLight` control the main light. See [models and cameras](./model), [environment lighting](./environment), [render modes](./render-mode), [AA](./antialiasing), and [parameter panels](./ui).

`EnableParameterPanel(bool)` toggles the optional panel; its configuration overload supplies [model and environment choices](./ui#model-and-environment-choices). For a complete application, see [writing examples](../guide/integration).
