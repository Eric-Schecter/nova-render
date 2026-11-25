<script setup>
import { ref } from 'vue'
import { compareTextureBindings } from '../../../web/examples/texture-bindings.js'

const canvas = ref(null)
const busy = ref(false)
const result = ref([])
const error = ref('')
const labels = {
  individual: 'Texture per draw',
  atlas: 'Texture atlas',
  layers: 'Texture array layers',
}

async function run() {
  busy.value = true
  result.value = []
  error.value = ''
  try {
    result.value = await compareTextureBindings(canvas.value)
  } catch (e) {
    error.value = e.message
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <div class="texture-bindings">
    <button :disabled="busy" @click="run">{{ busy ? 'Measuring…' : 'Run comparison' }}</button>
    <p v-if="error" role="alert">{{ error }}</p>
    <table v-if="result.length" aria-label="Texture binding results">
      <thead>
        <tr>
          <th>Strategy</th>
          <th>Draws</th>
          <th>Bind calls</th>
          <th>CPU (ms)</th>
          <th>GPU (ms)</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="row in result" :key="row.name">
          <td>{{ labels[row.name] }}</td>
          <td>{{ row.draws }}</td>
          <td>{{ row.binds }}</td>
          <td>{{ row.cpuMs.toFixed(3) }}</td>
          <td>{{ row.gpuMs?.toFixed(3) ?? 'Unavailable' }}</td>
        </tr>
      </tbody>
    </table>
    <canvas ref="canvas" aria-label="Identical output for all strategies" v-show="result.length" />
  </div>
</template>

<style scoped>
button {
  padding: 0.5rem 1rem;
  background: var(--vp-c-brand-soft);
  border: 1px solid var(--vp-c-brand-1);
  border-radius: 6px;
  color: var(--vp-c-brand-1);
}
button:disabled {
  opacity: 0.65;
}
canvas {
  display: block;
  width: 100%;
  max-width: 512px;
  height: auto;
}
</style>
