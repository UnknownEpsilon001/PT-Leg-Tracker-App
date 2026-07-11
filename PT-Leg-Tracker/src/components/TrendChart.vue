<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{ points: { label: string; value: number }[]; max: number }>()

const W = 320
const H = 160
const PAD = 24

const coords = computed(() =>
  props.points.map((p, i) => ({
    x:
      props.points.length === 1
        ? W / 2
        : PAD + (i * (W - 2 * PAD)) / (props.points.length - 1),
    y: H - PAD - (p.value / props.max) * (H - 2 * PAD),
    label: p.label,
  })),
)

const polyline = computed(() => coords.value.map((c) => `${c.x},${c.y}`).join(' '))
</script>

<template>
  <svg :viewBox="`0 0 ${W} ${H}`" class="chart" role="img">
    <line :x1="PAD" :y1="H - PAD" :x2="W - PAD" :y2="H - PAD" stroke="var(--c-border)" />
    <polyline :points="polyline" fill="none" stroke="var(--c-primary)" stroke-width="3" />
    <circle v-for="(c, i) in coords" :key="i" :cx="c.x" :cy="c.y" r="5" fill="var(--c-primary)" />
  </svg>
</template>

<style scoped>
.chart {
  width: 100%;
  height: auto;
}
</style>
