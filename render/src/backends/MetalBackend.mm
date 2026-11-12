#include "../Backend.hpp"
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace render::detail {
static std::runtime_error metalError(const char* operation, NSError* error) {
    return std::runtime_error(std::string(operation) + ": " +
        (error ? error.localizedDescription.UTF8String : "Metal resource unavailable"));
}
class MetalBackend final : public Backend {
public:
    ~MetalBackend() override {
        @autoreleasepool {
            if (lastCommand) [lastCommand waitUntilCompleted];
            if (view && layer && view.layer == layer) {
                view.layer = previousLayer;
                view.wantsLayer = previousWantsLayer;
            }
        }
    }
    void Initialize(GLFWwindow* window, const RendererConfig& config) {
        @autoreleasepool {
            device = MTLCreateSystemDefaultDevice();
            if (!device) throw std::runtime_error("No Metal device available");
            queue = [device newCommandQueue];
            if (!queue) throw std::runtime_error("Create Metal queue failed");
            NSWindow* native = glfwGetCocoaWindow(window);
            view = native.contentView;
            if (!view) throw std::runtime_error("GLFW window has no Cocoa content view");
            previousLayer = view.layer;
            previousWantsLayer = view.wantsLayer;
            layer = [CAMetalLayer layer];
            layer.device = device;
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
            layer.framebufferOnly = YES;
            layer.opaque = YES;
            layer.displaySyncEnabled = YES;
            layer.contentsScale = native.backingScaleFactor;
            layer.frame = view.bounds;
            view.wantsLayer = YES;
            view.layer = layer;
            const auto path = config.shaderDirectory / "triangle.metallib";
            NSError* error = nil;
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.string().c_str()]];
            id<MTLLibrary> library = [device newLibraryWithURL:url error:&error];
            if (!library) throw std::runtime_error("Load shader " + path.string() + ": " +
                (error ? error.localizedDescription.UTF8String : "Metal library unavailable"));
            MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
            desc.vertexFunction = [library newFunctionWithName:@"vertexMain"];
            desc.fragmentFunction = [library newFunctionWithName:@"fragmentMain"];
            desc.colorAttachments[0].pixelFormat = layer.pixelFormat;
            pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!pipeline) throw metalError("Create Metal pipeline", error);
            std::cout << "GPU: " << device.name.UTF8String << '\n';
            std::cout << "Metal command errors are counted; API validation is controlled by MTL_DEBUG_LAYER.\n";
        }
    }
    void Resize(uint32_t newWidth, uint32_t newHeight) override {
        if (width == newWidth && height == newHeight) return;
        width = newWidth; height = newHeight; resized = true;
    }
    FrameResult DrawFrame() override {
        if (!width || !height) return FrameResult::Skipped;
        @autoreleasepool {
            WaitIdle(); // One frame in flight, matching the other backends.
            if (resized) {
                layer.frame = view.bounds;
                layer.contentsScale = view.window.backingScaleFactor;
                layer.drawableSize = CGSizeMake(width, height);
                resized = false;
                std::cout << "Metal drawable: " << width << 'x' << height << '\n';
            }
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (!drawable) return FrameResult::Skipped;
            id<MTLCommandBuffer> command = [queue commandBuffer];
            if (!command) throw std::runtime_error("Create Metal command buffer failed");
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = drawable.texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0.025, 0.035, 0.055, 1.0);
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            if (!encoder) throw std::runtime_error("Create Metal render encoder failed");
            [encoder setRenderPipelineState:pipeline];
            [encoder setViewport:MTLViewport{0, 0, double(width), double(height), 0, 1}];
            [encoder setCullMode:MTLCullModeNone];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [encoder endEncoding];
            [command presentDrawable:drawable];
            [command commit];
            lastCommand = command;
            return FrameResult::Rendered;
        }
    }
    void WaitIdle() override {
        if (!lastCommand) return;
        [lastCommand waitUntilCompleted];
        if (lastCommand.status == MTLCommandBufferStatusError) {
            ++errors;
            NSError* error = lastCommand.error;
            lastCommand = nil;
            throw metalError("Execute Metal command buffer", error);
        }
        lastCommand = nil;
    }
    uint32_t ValidationErrorCount() const override { return errors; }
private:
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLRenderPipelineState> pipeline;
    id<MTLCommandBuffer> lastCommand;
    NSView* view = nil;
    CALayer* previousLayer = nil;
    CAMetalLayer* layer = nil;
    BOOL previousWantsLayer = NO;
    uint32_t width = 0, height = 0, errors = 0;
    bool resized = true;
};
std::unique_ptr<Backend> CreateMetalBackend(GLFWwindow* window, const RendererConfig& config) {
    auto backend = std::make_unique<MetalBackend>();
    backend->Initialize(window, config);
    return backend;
}
} // namespace render::detail
