import Foundation
import Metal

let side = 512, tile = 16, grid = 32, materialCount = 16
let device = MTLCreateSystemDefaultDevice()!
let queue = device.makeCommandQueue()!
let library = try device.makeLibrary(source: """
#include <metal_stdlib>
using namespace metal;

struct Vertex { float4 position [[position]]; float2 uv; uint material [[flat]]; };
vertex Vertex vs(uint vertexID [[vertex_id]], uint instance [[instance_id]]) {
    const float2 quad[] = {{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
    float2 uv = quad[vertexID];
    float2 xy = (float2(instance % 32, instance / 32) + uv) / 32;
    return {float4(xy * float2(2,-2) + float2(-1,1), 0, 1), uv, instance % 16};
}

fragment float4 individual(Vertex v [[stage_in]], texture2d<float> tex [[texture(0)]]) {
    return tex.read(uint2(v.uv * 16));
}
fragment float4 atlas(Vertex v [[stage_in]], texture2d<float> tex [[texture(0)]]) {
    return tex.read(uint2(v.uv * 16) + uint2(v.material * 16, 0));
}
struct Textures { array<texture2d<float>, 16> entries [[id(0)]]; };
fragment float4 indexed(Vertex v [[stage_in]], constant Textures& textures [[buffer(0)]]) {
    return textures.entries[v.material].read(uint2(v.uv * 16));
}
""", options: nil)

func texture(_ width: Int, _ height: Int, _ usage: MTLTextureUsage) -> MTLTexture {
    let desc = MTLTextureDescriptor.texture2DDescriptor(
        pixelFormat: .rgba8Unorm, width: width, height: height, mipmapped: false)
    desc.storageMode = .shared
    desc.usage = usage
    return device.makeTexture(descriptor: desc)!
}

let atlas = texture(tile * materialCount, tile, .shaderRead)
var textures: [MTLTexture] = []
for id in 0..<materialCount {
    let material = texture(tile, tile, .shaderRead)
    var bytes = [UInt8](repeating: 255, count: tile * tile * 4)
    for y in 0..<tile {
        for x in 0..<tile {
            let p = (y * tile + x) * 4
            bytes[p] = UInt8((id * 43 + x * 7) % 256)
            bytes[p + 1] = UInt8((id * 71 + y * 9) % 256)
            bytes[p + 2] = UInt8((x ^ y) * 16)
        }
    }
    material.replace(region: MTLRegionMake2D(0, 0, tile, tile), mipmapLevel: 0, withBytes: bytes, bytesPerRow: tile * 4)
    atlas.replace(region: MTLRegionMake2D(id * tile, 0, tile, tile), mipmapLevel: 0, withBytes: bytes, bytesPerRow: tile * 4)
    textures.append(material)
}
let arguments = library.makeFunction(name: "indexed")!.makeArgumentEncoder(bufferIndex: 0)
let table = device.makeBuffer(length: arguments.encodedLength, options: .storageModeShared)!
arguments.setArgumentBuffer(table, offset: 0)
for (id, material) in textures.enumerated() {
    arguments.setTexture(material, index: id)
}

let strategies = ["individual", "atlas", "indexed"]
let pipelines = try strategies.map { name in
    let desc = MTLRenderPipelineDescriptor()
    desc.vertexFunction = library.makeFunction(name: "vs")
    desc.fragmentFunction = library.makeFunction(name: name)
    desc.colorAttachments[0].pixelFormat = .rgba8Unorm
    return try device.makeRenderPipelineState(descriptor: desc)
}
let target = texture(side, side, .renderTarget)
let pass = MTLRenderPassDescriptor()
pass.colorAttachments[0].texture = target
pass.colorAttachments[0].loadAction = .clear
pass.colorAttachments[0].storeAction = .store

func draw(_ mode: Int) -> (Double, Double) {
    let start = DispatchTime.now().uptimeNanoseconds
    let commands = queue.makeCommandBuffer()!
    let encoder = commands.makeRenderCommandEncoder(descriptor: pass)!
    encoder.setRenderPipelineState(pipelines[mode])
    if mode == 1 { encoder.setFragmentTexture(atlas, index: 0) }
    if mode == 2 {
        encoder.setFragmentBuffer(table, offset: 0, index: 0)
        encoder.useResources(textures, usage: .read, stages: .fragment)
    }
    for i in 0..<(grid * grid) {
        if mode == 0 { encoder.setFragmentTexture(textures[i % materialCount], index: 0) }
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6, instanceCount: 1, baseInstance: i)
    }
    encoder.endEncoding()
    commands.commit()
    let cpu = Double(DispatchTime.now().uptimeNanoseconds - start) / 1e6
    commands.waitUntilCompleted()
    precondition(commands.status == .completed, String(describing: commands.error))
    return (cpu, (commands.gpuEndTime - commands.gpuStartTime) * 1e3)
}

var reference: [UInt8] = []
for mode in strategies.indices {
    _ = draw(mode)
    var bytes = [UInt8](repeating: 0, count: side * side * 4)
    target.getBytes(&bytes, bytesPerRow: side * 4, from: MTLRegionMake2D(0, 0, side, side), mipmapLevel: 0)
    if mode == 0 {
        precondition(bytes.enumerated().contains { $0.offset % 4 != 3 && $0.element != 0 })
        reference = bytes
    } else {
        precondition(bytes == reference, "Pixel mismatch for \(strategies[mode])")
    }
}
var cpu = [[Double]](repeating: [], count: strategies.count)
var gpu = cpu
for step in 0..<39 {
    for offset in strategies.indices {
        let mode = (step + offset) % strategies.count
        let sample = draw(mode)
        if step >= 9 {
            cpu[mode].append(sample.0)
            gpu[mode].append(sample.1)
        }
    }
}
print("\(device.name), \(grid * grid) draws, pixel-identical")
for (mode, name) in strategies.enumerated() {
    print(String(format: "%@: CPU %.3f ms, GPU %.3f ms", name, cpu[mode].sorted()[15], gpu[mode].sorted()[15]))
}
