<script setup>
import { ref } from 'vue'

const report = ref('')
const busy = ref(false)

async function query() {
  busy.value = true
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
    try {
      device = await adapter.requestDevice()
      const lines = [
        'Adapter',
        ...info,
        '',
        `Adapter features (${adapterFeatures.length})`,
        ...adapterFeatures,
        '',
        `WGSL language features (${language.length})`,
        ...language,
        '',
        'Limits (adapter / default device)',
        ...keys.map((key) => `${key} = ${limits[key]} / ${device.limits[key] ?? 'Unavailable'}`),
        '',
        'Default device features',
        ...[...device.features].sort(),
      ]
      report.value = lines.join('\n')
    } catch (error) {
      report.value = [
        'Adapter',
        ...info,
        '',
        `Adapter features (${adapterFeatures.length})`,
        ...adapterFeatures,
        '',
        `WGSL language features (${language.length})`,
        ...language,
        '',
        'Adapter limits',
        ...keys.map((key) => `${key} = ${limits[key]}`),
        '',
        `Default device unavailable: ${error.message}`,
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
  <pre v-if="report" aria-live="polite">{{ report }}</pre>
</template>

<style scoped>
button {
  padding: 6px 12px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
}
pre {
  max-height: 28rem;
  overflow: auto;
  padding: 12px;
  background: var(--vp-c-bg-soft);
}
</style>
