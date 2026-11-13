<script setup>
defineProps({ expanded: Boolean })
const emit = defineEmits(['update:expanded'])
</script>

<template>
  <div class="code-toolbar">
    <slot />
    <button type="button" :aria-expanded="expanded" @click="emit('update:expanded', !expanded)">
      {{ expanded ? '全部收起' : '全部展开' }}
    </button>
  </div>
</template>

<style scoped>
.code-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 8px;
}
button {
  flex-shrink: 0;
  padding: 4px 12px;
  border: 1px solid var(--vp-c-divider);
  border-radius: 6px;
  color: var(--vp-c-brand-1);
  font-size: 12px;
}
button:focus-visible {
  outline: 2px solid var(--vp-c-brand-1);
  outline-offset: 2px;
}
</style>
