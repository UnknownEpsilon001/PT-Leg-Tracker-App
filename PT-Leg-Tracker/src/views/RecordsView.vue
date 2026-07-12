<script setup lang="ts">
import { computed } from 'vue'
import AppIcon from '@/components/AppIcon.vue'
import TrendChart from '@/components/TrendChart.vue'
import { buildRecordRows } from '@/lib/records'
import { usePainLogStore } from '@/stores/painlog'
import { useSessionsStore } from '@/stores/sessions'

const store = useSessionsStore()
const painLogStore = usePainLogStore()

const rows = computed(() => buildRecordRows(store.sessions, painLogStore.painLogs))

const sorted = computed(() => [...rows.value].sort((a, b) => b.date.localeCompare(a.date)))

const painPoints = computed(() =>
  [...rows.value]
    .filter((r) => r.painAfter !== undefined)
    .sort((a, b) => a.date.localeCompare(b.date))
    .slice(-10)
    .map((r) => ({ label: r.date.slice(5, 10), value: r.painAfter as number })),
)

const thisWeekCount = computed(() => {
  const weekAgo = Date.now() - 7 * 24 * 3600 * 1000
  return rows.value.filter((r) => new Date(r.date).getTime() >= weekAgo).length
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
    <div v-for="r in sorted" :key="r.id" class="card row">
      <span>{{ fmt(r.date) }}</span>
      <span>{{ r.durationSec !== null ? mins(r.durationSec) + ' นาที' : 'บันทึกอาการ' }}</span>
      <span v-if="r.painAfter !== undefined">ปวด {{ r.painAfter }}/10</span>
      <span v-else>—</span>
      <span class="src"><AppIcon :name="r.source === 'device' ? 'wifi' : 'pencil'" /></span>
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
