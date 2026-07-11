<script setup lang="ts">
import { computed } from 'vue'
import TrendChart from '@/components/TrendChart.vue'
import { useSessionsStore } from '@/stores/sessions'

const store = useSessionsStore()

const sorted = computed(() =>
  [...store.sessions].sort((a, b) => b.date.localeCompare(a.date)),
)

const painPoints = computed(() =>
  [...store.sessions]
    .filter((s) => s.painAfter !== undefined)
    .sort((a, b) => a.date.localeCompare(b.date))
    .slice(-10)
    .map((s) => ({ label: s.date.slice(5, 10), value: s.painAfter as number })),
)

const thisWeekCount = computed(() => {
  const weekAgo = Date.now() - 7 * 24 * 3600 * 1000
  return store.sessions.filter((s) => new Date(s.date).getTime() >= weekAgo).length
})

function fmt(dateIso: string) {
  return new Date(dateIso).toLocaleDateString('th-TH', {
    day: 'numeric',
    month: 'short',
    year: 'numeric',
  })
}

function mins(sec: number) {
  return Math.round(sec / 60)
}
</script>

<template>
  <main class="page">
    <div class="card">
      <h2>สัปดาห์นี้ออกกำลังกาย {{ thisWeekCount }} ครั้ง</h2>
    </div>

    <div class="card" v-if="painPoints.length >= 2">
      <h2>ระดับความปวดหลังออกกำลังกาย (0-10)</h2>
      <TrendChart :points="painPoints" :max="10" />
    </div>

    <h2>ประวัติการออกกำลังกาย</h2>
    <p v-if="sorted.length === 0" class="card">ยังไม่มีบันทึก เริ่มออกกำลังกายครั้งแรกได้เลย</p>
    <div v-for="s in sorted" :key="s.id" class="card row">
      <span>{{ fmt(s.date) }}</span>
      <span>{{ mins(s.durationSec) }} นาที</span>
      <span v-if="s.painAfter !== undefined">ปวด {{ s.painAfter }}/10</span>
      <span v-else>—</span>
      <span class="src">{{ s.source === 'device' ? '📡' : '✍️' }}</span>
    </div>
  </main>
</template>

<style scoped>
.row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 0.5rem;
}

.src {
  font-size: 1.2rem;
}
</style>
