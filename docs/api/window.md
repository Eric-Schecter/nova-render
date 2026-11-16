# Window

The recommended application entry point, declared in `render/Window.hpp`. PImpl hides window dependencies; Window owns the event system, window and Renderer.

## Configuration

```cpp
struct WindowConfig {
    uint32_t width = 800;
    uint32_t height = 600;
    std::string title = "Nova Render";
    RendererConfig renderer;
};
```

Width and height are positive logical dimensions bounded by `int`; framebuffer pixel dimensions are queried internally. See [RendererConfig](./types).

## Methods

```cpp
explicit Window(const WindowConfig& config = {});
~Window();
bool PollEvents();
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
void EnableOrbitControls(bool enabled = true);
GraphicsAPI GetAPI() const;
uint32_t ValidationErrorCount() const;
```

- `PollEvents`: processes events; returns false after closing the window or pressing Esc.
- `DrawFrame`: synchronizes pixel dimensions and draws. Zero-size windows pause, and skipped frames briefly wait for events.
- Other methods forward to the internal [Renderer](./renderer).

Use only on the main thread. Copying and moving are disabled. Do not mix Window's GLFW lifecycle with an externally managed one. Construction validates configuration and cleans up on failure. Call `WaitIdle()` before exit; destruction releases Renderer, window and event system in that order.

`SetLighting(Unlit / PBR)` defaults to Unlit. Directional light, model, camera, environment, [AA](./antialiasing), [render mode](./render-mode), and [parameter panel](./ui) controls match Renderer. The panel configuration overload accepts application-owned asset lists.

## Application-managed views

`GetFramebufferSize()` returns pixel width and height, possibly zero when minimized. `PresentImage(image)` copies sRGB RGBA8 pixels for the next frame; `ClearImage()` restores Window's own rendering. The application manages layout, budgets and UI, as in [integrator comparison](../examples/integrators).

See [writing an example](../guide/integration) for a complete program. Use the lower-level Renderer when your application manages its own window.

`SetViewCamera` / `GetViewCamera` and `SetViewport` / `GetViewport` forward to the renderer. `GetFramebufferSize()` returns the pixel dimensions used by viewport rectangles. See [camera and viewport](./camera).

See [scene picking](./picking) for synchronous instance ray queries and hit data.
