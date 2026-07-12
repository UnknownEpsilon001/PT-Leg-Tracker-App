<script setup lang="ts">
import { computed, onUnmounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import AppIcon from '@/components/AppIcon.vue'
import PainScale from '@/components/PainScale.vue'
import { DeviceError, getStatus, startSession, stopSession } from '@/lib/device'
import { usePainLogStore } from '@/stores/painlog'
import { useSettingsStore } from '@/stores/settings'

const router = useRouter()
const painLogStore = usePainLogStore()
const settings = useSettingsStore()

const phase = ref<'before' | 'starting' | 'running' | 'after'>('before')
const painBefore = ref<number | null>(null)
const painAfter = ref<number | null>(null)
const sessionId = ref<string | null>(null)
const elapsedSec = ref<number | null>(null)
const connLost = ref(false)
const errorMsg = ref('')

let poller: ReturnType<typeof setInterval> | null = null

const deviceUrl = computed(() => settings.settings.deviceUrl.trim())

const clock = computed(() => {
  if (elapsedSec.value === null) return '--:--'
  const m = String(Math.floor(elapsedSec.value / 60)).padStart(2, '0')
  const s = String(elapsedSec.value % 60).padStart(2, '0')
  return `${m}:${s}`
})

function stopPolling() {
  if (poller) clearInterval(poller)
  poller = null
}

async function poll() {
  try {
    const status = await getStatus(deviceUrl.value)
    connLost.value = false
    if (status.state === 'running') {
      elapsedSec.value = status.elapsedSec
    } else {
      // device finished (by itself or via our stop)
      stopPolling()
      phase.value = 'after'
    }
  } catch {
    connLost.value = true // keep last known elapsed; keep polling
  }
}

async function start() {
  errorMsg.value = ''
  if (!deviceUrl.value) {
    errorMsg.value = 'ยังไม่ได้ตั้งค่าที่อยู่เครื่องบริหาร (ให้เจ้าหน้าที่ตั้งค่าในเมนูตั้งค่า)'
    return
  }
  phase.value = 'starting'
  try {
    sessionId.value = await startSession(deviceUrl.value)
    elapsedSec.value = 0
    phase.value = 'running'
    poller = setInterval(poll, 2000)
  } catch (e) {
    phase.value = 'before'
    errorMsg.value =
      e instanceof DeviceError && e.kind === 'busy'
        ? 'เครื่องกำลังทำงานอยู่แล้ว'
        : 'เชื่อมต่อเครื่องบริหารไม่ได้ ตรวจสอบว่าโทรศัพท์กับเครื่องอยู่ WiFi เดียวกัน'
  }
}

async function stop() {
  try {
    await stopSession(deviceUrl.value)
  } catch {
    // device idle or unreachable — polling/flow advances regardless
  }
  stopPolling()
  phase.value = 'after'
}

async function save() {
  if (painBefore.value === null || painAfter.value === null) return
  await painLogStore.add({
    sessionId: sessionId.value,
    recordedAt: new Date().toISOString(),
    painBefore: painBefore.value,
    painAfter: painAfter.value,
  })
  router.replace({ name: 'records' })
}

onUnmounted(stopPolling)
</script>

<template>
  <main class="page">
    <template v-if="phase === 'before' || phase === 'starting'">
      <div class="card">
        <h2>ก่อนเริ่ม ปวดเข่าแค่ไหน?</h2>
        <PainScale v-model="painBefore" />
      </div>
      <p class="card">สวมอุปกรณ์ให้เรียบร้อย (ดูวิธีใช้ได้ที่เมนู "วิธีใช้เครื่อง") แล้วกดเริ่ม</p>
      <p v-if="errorMsg" class="error">{{ errorMsg }}</p>
      <button
        class="primary wide"
        :disabled="painBefore === null || phase === 'starting'"
        @click="start"
      >
        <AppIcon name="play" />
        {{ phase === 'starting' ? 'กำลังสั่งเครื่อง...' : 'สั่งเครื่องเริ่มทำงาน' }}
      </button>
    </template>

    <template v-else-if="phase === 'running'">
      <p class="clock">{{ clock }}</p>
      <p class="hint">{{ connLost ? 'กำลังเชื่อมต่อ…' : 'เครื่องกำลังทำงาน บริหารเข่าตามจังหวะของอุปกรณ์' }}</p>
      <button class="primary wide" @click="stop"><AppIcon name="stop" /> สั่งเครื่องหยุด</button>
    </template>

    <template v-else>
      <div class="card">
        <h2>หลังออกกำลังกาย ปวดเข่าแค่ไหน?</h2>
        <PainScale v-model="painAfter" />
      </div>
      <button class="primary wide" :disabled="painAfter === null" @click="save">
        <AppIcon name="check" /> บันทึกผล
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

.error {
  color: #b91c1c;
  font-weight: 600;
}

.wide {
  width: 100%;
  font-size: 1.2rem;
  min-height: 4rem;
}
</style>
