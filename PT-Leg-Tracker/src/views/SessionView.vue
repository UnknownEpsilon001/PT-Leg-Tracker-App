<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import AppIcon from '@/components/AppIcon.vue'
import PainScale from '@/components/PainScale.vue'
import ConfettiBurst from '@/components/ConfettiBurst.vue'
import { DeviceError, claimSession, getCurrent, queueStart, queueStop } from '@/lib/device'
import { usePainLogStore } from '@/stores/painlog'
import { useProfileStore } from '@/stores/profile'
import { useSettingsStore } from '@/stores/settings'

const router = useRouter()
const painLogStore = usePainLogStore()
const profileStore = useProfileStore()
const settings = useSettingsStore()

const phase = ref<'before' | 'starting' | 'running' | 'after'>('before')
const painBefore = ref<number | null>(null)
const painAfter = ref<number | null>(null)
const sessionId = ref<string | null>(null)
const elapsedSec = ref<number | null>(null)
const reps = ref(0)
const attachAvailable = ref(false)
const connLost = ref(false)
const errorMsg = ref('')

let poller: ReturnType<typeof setInterval> | null = null
let ticker: ReturnType<typeof setInterval> | null = null
let disposed = false

const serverUrl = computed(() => settings.settings.serverUrl.trim())

const clock = computed(() => {
  if (elapsedSec.value === null) return '--:--'
  const m = String(Math.floor(elapsedSec.value / 60)).padStart(2, '0')
  const s = String(elapsedSec.value % 60).padStart(2, '0')
  return `${m}:${s}`
})

function stopPolling() {
  if (poller) clearInterval(poller)
  poller = null
  if (ticker) clearInterval(ticker)
  ticker = null
}

function startPolling() {
  stopPolling()
  poller = setInterval(poll, 2000)
  // smooth 1 s clock between polls; each poll overwrites with the server value
  ticker = setInterval(() => {
    if (phase.value === 'running' && elapsedSec.value !== null) elapsedSec.value++
  }, 1000)
}

async function poll() {
  try {
    const cur = await getCurrent(serverUrl.value)
    if (disposed) return
    connLost.value = false
    if (cur.state === 'running') {
      phase.value = 'running'
      sessionId.value = cur.sessionId
      elapsedSec.value = cur.elapsedSec
      reps.value = cur.reps
    } else if (cur.state === 'starting') {
      if (!cur.deviceOnline) failOffline()
    } else if (phase.value === 'starting') {
      // command expired before the device ever confirmed → device offline
      failOffline()
    } else {
      // device finished (by itself or via our stop)
      stopPolling()
      phase.value = 'after'
    }
  } catch {
    connLost.value = true // keep last-known values; keep polling
  }
}

function failOffline() {
  stopPolling()
  phase.value = 'before'
  errorMsg.value = 'เครื่องออฟไลน์'
}

async function start() {
  errorMsg.value = ''
  if (!serverUrl.value) {
    errorMsg.value = 'ยังไม่ได้ตั้งค่าที่อยู่เซิร์ฟเวอร์ (ให้เจ้าหน้าที่ตั้งค่าในเมนูตั้งค่า)'
    return
  }
  phase.value = 'starting'
  attachAvailable.value = false
  try {
    await queueStart(serverUrl.value)
    if (disposed) return
    startPolling()
  } catch (e) {
    if (disposed) return
    phase.value = 'before'
    if (e instanceof DeviceError && e.kind === 'busy') {
      errorMsg.value = 'เครื่องกำลังทำงานอยู่แล้ว'
      attachAvailable.value = true
    } else {
      errorMsg.value = 'เชื่อมต่อไม่สำเร็จ ลองใหม่ภายหลัง'
    }
  }
}

function attach() {
  errorMsg.value = ''
  attachAvailable.value = false
  phase.value = 'running'
  startPolling()
  void poll()
}

async function stop() {
  try {
    await queueStop(serverUrl.value)
  } catch {
    // session already over or server unreachable — flow advances regardless
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
  const code = profileStore.profile?.patientCode
  if (sessionId.value && code && serverUrl.value) {
    try {
      await claimSession(serverUrl.value, sessionId.value, code)
    } catch {
      // best-effort: session stays unclaimed on the server, local log is saved
    }
  }
  router.replace({ name: 'records' })
}

onMounted(async () => {
  if (!serverUrl.value) return
  try {
    const cur = await getCurrent(serverUrl.value)
    if (!disposed && cur.state === 'running' && phase.value === 'before')
      attachAvailable.value = true
  } catch {
    // probe is best-effort; start() reports connection errors
  }
})

onUnmounted(() => {
  disposed = true
  stopPolling()
})
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
        class="primary wide breathe"
        :disabled="painBefore === null || phase === 'starting'"
        @click="start"
      >
        <AppIcon name="play" />
        {{ phase === 'starting' ? 'กำลังสั่งเครื่อง...' : 'สั่งเครื่องเริ่มทำงาน' }}
      </button>
      <button
        v-if="attachAvailable"
        class="wide"
        :disabled="painBefore === null || phase === 'starting'"
        @click="attach"
      >
        <AppIcon name="play" /> เข้าร่วมเซสชันที่กำลังทำงานอยู่
      </button>
    </template>

    <template v-else-if="phase === 'running'">
      <div class="clock-ring">
        <p class="clock">{{ clock }}</p>
      </div>
      <p class="reps">
        จำนวนครั้ง:
        <Transition name="pop" mode="out-in">
          <span :key="reps" class="reps-num">{{ reps }}</span>
        </Transition>
      </p>
      <p class="hint">
        {{ connLost ? 'กำลังเชื่อมต่อ…' : 'เครื่องกำลังทำงาน บริหารเข่าตามจังหวะของอุปกรณ์' }}
      </p>
      <button class="accent wide" @click="stop"><AppIcon name="stop" /> สั่งเครื่องหยุด</button>
    </template>

    <template v-else>
      <ConfettiBurst />
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

.clock-ring {
  width: 14rem;
  height: 14rem;
  margin: 2rem auto 0.5rem;
  border-radius: 50%;
  border: 4px solid var(--c-primary);
  display: flex;
  align-items: center;
  justify-content: center;
}

.clock-ring .clock {
  margin: 0;
  font-size: 3rem;
}

.reps-num {
  display: inline-block;
  font-weight: 700;
  color: var(--c-primary);
}

@media (prefers-reduced-motion: no-preference) {
  .clock-ring {
    animation: ring-pulse 2s ease-in-out infinite;
  }

  @keyframes ring-pulse {
    0%,
    100% {
      box-shadow: 0 0 0 0 rgb(36 57 94 / 0.35);
    }
    50% {
      box-shadow: 0 0 0 14px rgb(36 57 94 / 0);
    }
  }

  .pop-enter-active {
    animation: rep-pop 0.3s ease-out;
  }

  @keyframes rep-pop {
    0% {
      transform: scale(1.6);
    }
    100% {
      transform: scale(1);
    }
  }

  .breathe {
    animation: breathe 2.6s ease-in-out infinite;
  }

  @keyframes breathe {
    0%,
    100% {
      transform: scale(1);
    }
    50% {
      transform: scale(1.02);
    }
  }
}

.reps {
  font-size: 1.5rem;
  text-align: center;
  font-variant-numeric: tabular-nums;
  margin-bottom: 0.5rem;
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
