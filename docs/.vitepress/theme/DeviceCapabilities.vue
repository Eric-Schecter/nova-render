<script setup>
import { ref } from 'vue'

const report = ref('')
const busy = ref(false)
const sections = ref([])

const limitGroups = [
  [
    'Textures and sampling',
    {
      maxTextureDimension1D: 'Lookup tables stored in 1D textures.',
      maxTextureDimension2D: 'Material textures, shadow maps, and offscreen render targets.',
      maxTextureDimension3D: 'Volumetric fog, voxel grids, and 3D lookup tables.',
      maxTextureArrayLayers: 'Texture arrays for cascaded shadows and grouped material textures.',
      maxSampledTexturesPerShaderStage:
        'Source textures a shader stage can bind, such as material maps and G-buffer inputs.',
      maxSamplersPerShaderStage:
        'Filtering and address-mode configurations available to a shader stage.',
      maxStorageTexturesPerShaderStage:
        'Writable images used for compute post-processing and denoising.',
    },
  ],
  [
    'Buffers and resource bindings',
    {
      maxBufferSize:
        'Maximum allocation size for one buffer, such as mesh data or an instance pool.',
      maxStorageBufferBindingSize:
        'Maximum range visible through one storage binding, such as a material table or ray-tracing BVH.',
      maxUniformBufferBindingSize:
        'Maximum range for camera, lighting, or other uniform parameters in one binding.',
      minUniformBufferOffsetAlignment:
        'Alignment required when placing multiple uniform records in one buffer.',
      minStorageBufferOffsetAlignment:
        'Alignment required when binding a subrange of a storage buffer.',
      maxStorageBuffersPerShaderStage:
        'Number of storage buffers a stage can bind for scene data, culling, or skinning.',
      maxUniformBuffersPerShaderStage: 'Number of uniform buffers a shader stage can bind.',
      maxBindGroups:
        'Resource groups that a pipeline can organize, such as frame, scene, and material bindings.',
      maxBindingsPerBindGroup: 'Available binding slots within each resource group.',
      maxDynamicUniformBuffersPerPipelineLayout:
        'Uniform bindings whose offsets can change between draws.',
      maxDynamicStorageBuffersPerPipelineLayout:
        'Storage bindings whose offsets can change between draws or dispatches.',
    },
  ],
  [
    'Geometry and raster output',
    {
      maxVertexBuffers:
        'Separate vertex streams for positions, normals, skinning weights, and other attributes.',
      maxVertexAttributes: 'Attribute inputs available to a vertex shader.',
      maxVertexBufferArrayStride: 'Maximum byte spacing between consecutive vertex records.',
      maxInterStageShaderVariables:
        'Values passed from the vertex to the fragment stage, such as UVs and normals.',
      maxColorAttachments:
        'Simultaneous color outputs, such as the attachments of a deferred G-buffer.',
      maxColorAttachmentBytesPerSample:
        'Combined color output budget per sample, which constrains G-buffer format choices.',
    },
  ],
  [
    'Compute shaders',
    {
      maxComputeInvocationsPerWorkgroup:
        'Total threads in a group for GPU culling, skinning, or screen-space filters.',
      maxComputeWorkgroupSizeX: 'Maximum group size along X, useful for 1D culling and compaction.',
      maxComputeWorkgroupSizeY: 'Maximum group size along Y, useful for 2D image tiles.',
      maxComputeWorkgroupSizeZ: 'Maximum group size along Z, useful for 3D volumes.',
      maxComputeWorkgroupsPerDimension:
        'Maximum group count per dispatch axis, which can require splitting large workloads.',
      maxComputeWorkgroupStorageSize:
        'Shared bytes per workgroup for reductions, scans, and tile caches.',
    },
  ],
]

const featureUses = {
  'timestamp-query': 'Measure GPU pass time for profiling.',
  subgroups: 'Coordinate shader lanes for reductions and compaction.',
  'shader-f16': 'Use half-precision values in compatible shader calculations and data.',
  'indirect-first-instance': 'Read a nonzero first instance from an indirect draw command.',
  'depth-clip-control': 'Control depth clipping in raster pipelines.',
  'depth32float-stencil8': 'Use 32-bit depth with an 8-bit stencil attachment.',
  'float32-filterable': 'Filter supported 32-bit float textures.',
  'float32-blendable': 'Blend into supported 32-bit float color targets.',
  'texture-compression-bc': 'Sample BC-compressed material textures.',
  'texture-compression-etc2': 'Sample ETC2-compressed material textures.',
  'texture-compression-astc': 'Sample ASTC-compressed material textures.',
}

async function query() {
  busy.value = true
  sections.value = []
  try {
    const adapter = await navigator.gpu?.requestAdapter()
    if (!adapter) {
      report.value = 'WebGPU adapter unavailable.'
      return
    }
    const limits = adapter.limits
    const keys = Object.getOwnPropertyNames(Object.getPrototypeOf(limits))
      .filter((key) => typeof limits[key] === 'number')
      .sort()
    const infoKeys = [
      'vendor',
      'architecture',
      'device',
      'description',
      'isFallbackAdapter',
      'subgroupMinSize',
      'subgroupMaxSize',
    ]
    const info = infoKeys
      .filter((key) => adapter.info?.[key] !== undefined && adapter.info[key] !== '')
      .map((key) => `${key} = ${adapter.info[key]}`)
    const adapterFeatures = [...adapter.features].sort()
    const language = [...(navigator.gpu.wgslLanguageFeatures ?? [])].sort()
    let device
    let deviceError
    try {
      device = await adapter.requestDevice()
    } catch (error) {
      deviceError = error.message
    }

    try {
      const known = new Set(limitGroups.flatMap(([, entries]) => Object.keys(entries)))
      const groups = [
        ...limitGroups,
        [
          'Other limits',
          Object.fromEntries(keys.filter((key) => !known.has(key)).map((key) => [key, ''])),
        ],
      ]
      sections.value = groups
        .map(([title, entries]) => ({
          title,
          rows: Object.entries(entries)
            .filter(([key]) => keys.includes(key))
            .map(([key, use]) => ({
              key,
              use,
              adapter: limits[key],
              device: device?.limits[key] ?? '—',
            })),
        }))
        .filter(({ rows }) => rows.length)
      report.value = [
        'Adapter',
        ...info,
        '',
        `Adapter features (${adapterFeatures.length})`,
        ...adapterFeatures.map((key) => (featureUses[key] ? `${key} — ${featureUses[key]}` : key)),
        '',
        `WGSL language features (${language.length})`,
        ...language,
        '',
        ...(device
          ? ['Default device features', ...[...device.features].sort()]
          : [`Default device unavailable: ${deviceError}`]),
      ].join('\n')
    } finally {
      device?.destroy()
    }
  } catch (error) {
    report.value = error.message
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <button type="button" :disabled="busy" @click="query">
    {{ busy ? 'Querying…' : 'Query WebGPU adapter' }}
  </button>
  <p>
    WebGPU does not expose total VRAM, current VRAM usage, or the GPU memory budget. Buffer limits
    describe permitted resource sizes, and desktop adapter reports can show memory information.
  </p>
  <div v-if="sections.length">
    <details v-for="section in sections" :key="section.title" open>
      <summary>{{ section.title }}</summary>
      <table>
        <thead>
          <tr>
            <th>Limit and typical use</th>
            <th>Adapter</th>
            <th>Default device</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="row in section.rows" :key="row.key">
            <td>
              <code>{{ row.key }}</code
              ><br v-if="row.use" />{{ row.use }}
            </td>
            <td>{{ row.adapter }}</td>
            <td>{{ row.device }}</td>
          </tr>
        </tbody>
      </table>
    </details>
  </div>
  <details v-if="report && sections.length">
    <summary>Adapter and shader features</summary>
    <pre>{{ report }}</pre>
  </details>
  <p v-else-if="report" role="status">{{ report }}</p>
</template>

<style scoped>
button {
  padding: 6px 12px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
}
summary {
  cursor: pointer;
  font-weight: 600;
}
details {
  margin-block: 1rem;
}
pre {
  max-height: 28rem;
  overflow: auto;
  padding: 12px;
  background: var(--vp-c-bg-soft);
}
</style>
