<script setup lang="ts">
import { computed, onUnmounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import PainScale from '@/components/PainScale.vue'
import { useSessionsStore } from '@/stores/sessions'

const router = useRouter()
const store = useSessionsStore()

const phase = ref<'ready' | 'running' | 'rate'>('ready')
const painBefore = ref<number | null>(null)
const painAfter = ref<number | null>(null)
const seconds = ref(0)
let timer: ReturnType<typeof setInterval> | null = null

const clock = computed(() => {
  const m = String(Math.floor(seconds.value / 60)).padStart(2, '0')
  const s = String(seconds.value % 60).padStart(2, '0')
  return `${m}:${s}`
})

function start() {
  phase.value = 'running'
  timer = setInterval(() => seconds.value++, 1000)
}

function stop() {
  if (timer) clearInterval(timer)
  phase.value = 'rate'
}

async function save() {
  if (painAfter.value === null) return
  await store.add({
    id: `manual-${Date.now()}`,
    date: new Date().toISOString(),
    durationSec: seconds.value,
    painBefore: painBefore.value ?? undefined,
    painAfter: painAfter.value,
    source: 'manual',
  })
  router.replace({ name: 'records' })
}

onUnmounted(() => {
  if (timer) clearInterval(timer)
})
</script>

<template>
  <main class="page">
    <template v-if="phase === 'ready'">
      <div class="card">
        <h2>ก่อนเริ่ม ปวดเข่าแค่ไหน?</h2>
        <PainScale v-model="painBefore" />
      </div>
      <p class="card">สวมอุปกรณ์ให้เรียบร้อย (ดูวิธีใช้ได้ที่เมนู "วิธีใช้เครื่อง") แล้วกดเริ่ม</p>
      <button class="primary wide" @click="start">▶ เริ่มออกกำลังกาย</button>
    </template>

    <template v-else-if="phase === 'running'">
      <p class="clock">{{ clock }}</p>
      <p class="hint">บริหารเข่าตามจังหวะของอุปกรณ์</p>
      <button class="primary wide" @click="stop">⏹ เสร็จแล้ว</button>
    </template>

    <template v-else>
      <div class="card">
        <h2>หลังออกกำลังกาย ปวดเข่าแค่ไหน?</h2>
        <PainScale v-model="painAfter" />
      </div>
      <button class="primary wide" :disabled="painAfter === null" @click="save">
        ✔ บันทึกผล
      </button>
    </template>
  </main>
</template>

<style scoped>
.clock {
  font-size: 4rem;
  text-align: center;
  font-variant-numeric: tabular-nums;
  margin: 2rem 0 0.5rem;
}

.hint {
  text-align: center;
  margin-bottom: 2rem;
}

.wide {
  width: 100%;
  font-size: 1.2rem;
  min-height: 4rem;
}
</style>
