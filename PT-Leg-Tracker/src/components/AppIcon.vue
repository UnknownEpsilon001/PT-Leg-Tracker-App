<script setup lang="ts">
import { computed } from 'vue'
import { ICONS } from '@/lib/icons'

const props = defineProps<{ name: string }>()
const paths = computed(() => ICONS[props.name] ?? [])
</script>

<template>
  <svg
    v-if="paths.length"
    class="app-icon"
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    stroke-width="2.25"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
  >
    <path v-for="(d, i) in paths" :key="i" :d="d" />
  </svg>
</template>

<style scoped>
.app-icon {
  width: 1.35em;
  height: 1.35em;
  flex-shrink: 0;
  vertical-align: -0.25em;
}
</style>
