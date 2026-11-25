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
    const lines = ['Adapter']
    for (const name of ['vendor', 'architecture', 'device', 'description', 'isFallbackAdapter'])
      if (adapter.info?.[name] !== undefined && adapter.info[name] !== '')
        lines.push(`${name} = ${adapter.info[name]}`)
    lines.push('', 'Features', ...[...adapter.features].sort(), '', 'Limits')
    const keys = Object.getOwnPropertyNames(Object.getPrototypeOf(adapter.limits))
      .filter((key) => typeof adapter.limits[key] === 'number')
      .sort()
    for (const key of keys) lines.push(`${key} = ${adapter.limits[key]}`)
    report.value = lines.join('\n')
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
