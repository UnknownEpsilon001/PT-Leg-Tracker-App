<script setup lang="ts">
// หน้าบันทึกของฉัน: รวมเซสชันกับบันทึกความปวดเป็นตาราง พร้อมกราฟแนวโน้ม
// Records screen: sessions + pain logs as a table, with a trend chart.
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
    .map((r) => ({ label: localMonthDay(r.date), value: r.painAfter as number })),
)

// MM-DD on the phone's own calendar; slicing the ISO string gave the UTC day,
// a day early for anything logged before 07:00 in Thailand
// [TH] เดือน-วัน ตามเวลาเครื่อง ; ตัดจากสตริง ISO จะได้วันของ UTC ซึ่งช้าไปหนึ่งวันก่อน 7 โมงเช้า
function localMonthDay(iso: string) {
  const d = new Date(iso)
  return `${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
}

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
    <div
      v-for="(r, i) in sorted"
      :key="r.id"
      class="card row stagger"
      :style="{ '--i': Math.min(i, 8) }"
    >
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
