#include "../Backend.hpp"
#include "../ShaderData.hpp"
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
            settings = config;
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
            const auto bytes = LoadShader(config.shaderDirectory, "triangle.metallib");
            NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
            dispatch_data_t shaderData = dispatch_data_create(data.bytes, data.length,
                dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{ (void)data; });
            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithData:shaderData error:&error];
            if (!library) throw metalError("Load triangle.metallib", error);
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
            if (model && gpuModel != model.get()) UploadModel();
            if (model && (!depth || depth.width != width || depth.height != height)) {
                MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:width height:height mipmapped:NO];
                desc.storageMode = MTLStorageModePrivate; desc.usage = MTLTextureUsageRenderTarget;
                depth = [device newTextureWithDescriptor:desc];
                if (!depth) throw std::runtime_error("Create model depth texture failed");
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
            if (model) {
                pass.depthAttachment.texture = depth;
                pass.depthAttachment.loadAction = MTLLoadActionClear;
                pass.depthAttachment.storeAction = MTLStoreActionDontCare;
                pass.depthAttachment.clearDepth = 1.0;
            }
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            if (!encoder) throw std::runtime_error("Create Metal render encoder failed");
            [encoder setRenderPipelineState:model ? modelPipeline : pipeline];
            [encoder setViewport:MTLViewport{0, 0, double(width), double(height), 0, 1}];
            [encoder setCullMode:MTLCullModeNone];
            if (model) {
                const auto uniforms = MakeSceneUniforms(*model, camera, float(width)/height,lighting);
                [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:0];
                [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
                [encoder setVertexBuffer:vertices offset:0 atIndex:1];
                for (NSUInteger i=0;i<5;++i) [encoder setFragmentTexture:modelTextures[i] atIndex:2+i];
                [encoder setFragmentSamplerState:modelSampler atIndex:7];
                [encoder setDepthStencilState:depthState];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:model->vertices.size()];
            } else [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
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
    RendererConfig settings;
    const ModelData* gpuModel = nullptr;
    id<MTLBuffer> vertices;
    id<MTLRenderPipelineState> modelPipeline;
    id<MTLDepthStencilState> depthState;
    id<MTLTexture> depth;
    std::array<id<MTLTexture>,5> modelTextures{};
    id<MTLSamplerState> modelSampler;
    void UploadModel() {
        const auto shader = LoadShader(settings.shaderDirectory, "model.metallib");
        NSData* data = [NSData dataWithBytes:shader.data() length:shader.size()];
        dispatch_data_t blob = dispatch_data_create(data.bytes, data.length, dispatch_get_global_queue(QOS_CLASS_DEFAULT,0), ^{ (void)data; });
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithData:blob error:&error];
        if (!library) throw metalError("Load model.metallib",error);
        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = [library newFunctionWithName:@"vertexMain"];
        desc.fragmentFunction = [library newFunctionWithName:@"fragmentMain"];
        desc.colorAttachments[0].pixelFormat = layer.pixelFormat;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        modelPipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!modelPipeline) throw metalError("Create model pipeline",error);
        vertices = [device newBufferWithBytes:model->vertices.data() length:model->vertices.size()*sizeof(ModelVertex) options:MTLResourceStorageModeShared];
        if (!vertices) throw std::runtime_error("Create model vertex buffer failed");
        for (size_t i=0;i<5;++i) {
            const auto& image=model->images[i];
            MTLTextureDescriptor* texture=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:image.width height:image.height mipmapped:NO];
            texture.usage=MTLTextureUsageShaderRead; texture.storageMode=MTLStorageModeShared;
            modelTextures[i]=[device newTextureWithDescriptor:texture];
            if (!modelTextures[i]) throw std::runtime_error("Create model material texture failed");
            [modelTextures[i] replaceRegion:MTLRegionMake2D(0,0,image.width,image.height) mipmapLevel:0 withBytes:image.pixels.data() bytesPerRow:image.width*4];
        }
        MTLSamplerDescriptor* sampler=[MTLSamplerDescriptor new];
        sampler.minFilter=MTLSamplerMinMagFilterLinear; sampler.magFilter=MTLSamplerMinMagFilterLinear;
        sampler.sAddressMode=MTLSamplerAddressModeRepeat; sampler.tAddressMode=MTLSamplerAddressModeRepeat;
        modelSampler=[device newSamplerStateWithDescriptor:sampler];
        MTLDepthStencilDescriptor* depthDesc=[MTLDepthStencilDescriptor new];
        depthDesc.depthCompareFunction=MTLCompareFunctionLess; depthDesc.depthWriteEnabled=YES;
        depthState=[device newDepthStencilStateWithDescriptor:depthDesc];
        if (!modelSampler || !depthState) throw std::runtime_error("Create model sampler/depth state failed");
        gpuModel=model.get();
    }
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
