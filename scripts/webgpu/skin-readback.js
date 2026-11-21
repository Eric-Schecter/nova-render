// Validation only: execute the same HLSL skinVertex function as the render pipeline.
export async function createSkinReadback(device, directory = '/generated/') {
  const response = await fetch(directory + 'skinning.json')
  if (!response.ok) throw Error('Missing GPU skin validation shader')
  const shader = await response.json()
  const module = device.createShaderModule({ code: shader.compute })
  const info = await module.getCompilationInfo()
  if (info.messages.some((m) => m.type === 'error')) throw Error('Skin validation WGSL failed')
  const layout = device.createBindGroupLayout({
    entries: [0, 1, 11, 12, 13].map((binding) => ({
      binding,
      visibility: GPUShaderStage.COMPUTE,
      buffer: {
        type: binding === 0 ? 'uniform' : binding === 13 ? 'storage' : 'read-only-storage',
      },
    })),
  })
  const pipeline = await device.createComputePipelineAsync({
    layout: device.createPipelineLayout({ bindGroupLayouts: [layout] }),
    compute: { module, entryPoint: 'SkinningCSMain' },
  })
  return async (data) => {
    const scene = new Float32Array(96)
    scene[59] = 1
    scene[58] = data.vertices.length / 20
    const resources = []
    const buffer = (bytes, usage) => {
      const b = device.createBuffer({
        size: bytes.byteLength,
        usage: usage | GPUBufferUsage.COPY_DST,
      })
      resources.push(b)
      device.queue.writeBuffer(b, 0, bytes)
      return b
    }
    try {
      const buffers = [
        buffer(scene, GPUBufferUsage.UNIFORM),
        buffer(data.vertices, GPUBufferUsage.STORAGE),
        buffer(data.skinInputs, GPUBufferUsage.STORAGE),
        buffer(data.jointMatrices, GPUBufferUsage.STORAGE),
        buffer(
          new Uint8Array(data.vertices.byteLength),
          GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
        ),
      ]
      const group = device.createBindGroup({
        layout,
        entries: [0, 1, 11, 12, 13].map((binding, i) => ({
          binding,
          resource: { buffer: buffers[i] },
        })),
      })
      const read = device.createBuffer({
        size: data.vertices.byteLength,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
      })
      resources.push(read)
      const encoder = device.createCommandEncoder(),
        pass = encoder.beginComputePass()
      pass.setPipeline(pipeline)
      pass.setBindGroup(0, group)
      pass.dispatchWorkgroups(Math.ceil(data.vertices.length / 20 / 64))
      pass.end()
      encoder.copyBufferToBuffer(buffers[4], 0, read, 0, data.vertices.byteLength)
      device.queue.submit([encoder.finish()])
      await read.mapAsync(GPUMapMode.READ)
      return new Float32Array(read.getMappedRange().slice(0))
    } finally {
      for (const b of resources) b.destroy()
    }
  }
}
