<script setup lang="ts">
// หน้าออกกำลังกาย: สั่งเริ่ม/หยุดเครื่องผ่านเซิร์ฟเวอร์ แสดงเวลา/จำนวนรอบสด บันทึกความปวดก่อน-หลัง
// Exercise screen: start/stop the device via the server, live timer/reps, before/after pain logging.
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import AppIcon from '@/components/AppIcon.vue'
import PainScale from '@/components/PainScale.vue'
import ConfettiBurst from '@/components/ConfettiBurst.vue'
import { DeviceError, claimSession, getCurrent, queueStart, queueStop, setTarget } from '@/lib/device'
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
// null = we have not yet learned the machine's real target. Never guess a
// number here — an explicit 0 sent to the device means "run until STOP",
// so an unlearned target must send no body at all (see start()) rather
// than silently commanding an unlimited session.
// [TH] null = ยังไม่รู้ค่าเป้าหมายจริงของเครื่อง ห้ามเดาเป็นตัวเลข เพราะถ้าส่ง 0 ไปตรงๆ
//      จะแปลว่า "ทำไปเรื่อยๆ ไม่จำกัด" ดังนั้นถ้ายังไม่รู้ค่า ตอนเริ่ม (start()) จะไม่ส่งค่านี้ไปเลย
//      แทนที่จะสั่งแบบไม่จำกัดโดยที่ผู้ใช้ไม่ได้ตั้งใจ
const target = ref<number | null>(null)
// Distinguishes *why* target is null: "never learned a value" (false) vs "an
// edit just succeeded on the server but the read that would confirm the new
// number failed" (true). The second case still renders as unknown — the app
// declines to assert a number it cannot vouch for, so start() sends no body
// and the edit that already took effect server-side survives — but the
// wording tells the user their tap worked rather than looking like it broke.
// [TH] แยกว่า "ทำไม" ค่าเป้าหมายถึงเป็น null: "ยังไม่เคยรู้ค่าเลย" (false) กับ "เพิ่งแก้ค่าสำเร็จ
//      ที่เซิร์ฟเวอร์ แต่การอ่านยืนยันค่าใหม่ล้มเหลว" (true) กรณีหลังก็ยังแสดงเป็น "ไม่ทราบ" เหมือนกัน
//      แอปจะไม่ยืนยันตัวเลขที่พิสูจน์ไม่ได้ ตอนกดเริ่มจะไม่ส่งค่านี้ไป การแก้ไขที่สำเร็จแล้วที่เซิร์ฟเวอร์
//      จะได้ไม่ถูกทับ แต่ข้อความจะบอกผู้ใช้ว่าที่กดไปนั้นสำเร็จ ไม่ใช่ว่าพัง
const targetUnconfirmed = ref(false)
const targetLabel = computed(() => {
  if (target.value === null)
    return targetUnconfirmed.value ? 'ส่งค่าไปแล้ว แต่ยืนยันค่าจริงไม่ได้' : 'ไม่ทราบค่า'
  return target.value > 0 ? `${target.value} ครั้ง` : 'ไม่จำกัด'
})
const attachAvailable = ref(false)
const connLost = ref(false)
const errorMsg = ref('')

let poller: ReturnType<typeof setInterval> | null = null
let ticker: ReturnType<typeof setInterval> | null = null
let disposed = false

const serverUrl = computed(() => settings.settings.serverUrl.trim())
const deviceCode = computed(() => settings.settings.deviceCode.trim())

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
    const cur = await getCurrent(serverUrl.value, deviceCode.value)
    if (disposed) return
    connLost.value = false
    // The target is the machine's, not ours — every successful poll re-reads it.
    // [TH] เป้าหมายเป็นของเครื่อง ไม่ใช่ของแอป การอ่านสถานะทุกครั้งที่สำเร็จจะอัปเดตค่าตามจริงเสมอ
    target.value = cur.targetReps
    targetUnconfirmed.value = false
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

// The controller owns the target: this sends the change, then re-reads /current
// once to bring the real value back, so the phone stays in sync with the
// machine even when the target was changed on the machine's own screen. The
// re-read (not a local echo of `next`) is what lets this run before a session
// has started, when no poll loop exists yet to pick the change up on its own —
// without it, a successful edit here would sit unseen until start() sent the
// stale pre-edit number back to the device, silently undoing the user's tap.
// [TH] ค่าเป้าหมายเป็นของเครื่อง ตรงนี้ส่งคำขอเปลี่ยนแล้วอ่านสถานะ /current ซ้ำอีกครั้งเพื่อดึงค่าจริงกลับมา
//      หน้าจอจะได้ตรงกับเครื่องเสมอ รวมถึงตอนที่มีคนเปลี่ยนค่าที่หน้าจอ CYD การอ่านซ้ำ (ไม่ใช่การจำค่า next ไว้เอง)
//      คือสิ่งที่ทำให้ใช้งานได้ก่อนเริ่มเซสชัน ตอนที่ยังไม่มีลูปอ่านสถานะทำงานอยู่ ถ้าไม่อ่านซ้ำ การกดที่สำเร็จ
//      จะไม่ถูกแสดงเลย จนกว่า start() จะส่งค่าเก่าก่อนแก้ไขกลับไปทับโดยที่ผู้ใช้ไม่รู้ตัว
let targetOpSeq = 0 // guards against a slower earlier tap's re-read landing after a later tap's
async function stepTarget(delta: number) {
  if (target.value === null) return // unknown baseline: wait for a real value first
  const next = Math.min(99, Math.max(0, target.value + delta))
  if (next === target.value) return
  const opSeq = ++targetOpSeq
  try {
    await setTarget(serverUrl.value, deviceCode.value, next)
  } catch {
    // a failed change is simply not applied; the next poll re-reads the truth
    // [TH] ถ้าเปลี่ยนไม่สำเร็จก็ถือว่าไม่เปลี่ยน การอ่านสถานะรอบถัดไปจะดึงค่าจริงกลับมาเอง
    return
  }
  try {
    const cur = await getCurrent(serverUrl.value, deviceCode.value)
    if (disposed) return
    // A newer tap has since started its own request — let that one's result
    // win instead of overwriting it with this now-stale read.
    // [TH] ถ้ามีการกดครั้งใหม่กว่าเริ่มไปแล้ว ให้ผลของครั้งใหม่ชนะ ไม่เอาค่าเก่าที่เพิ่งอ่านมาทับ
    if (opSeq !== targetOpSeq) return
    target.value = cur.targetReps
    targetUnconfirmed.value = false
  } catch {
    if (disposed) return
    if (opSeq !== targetOpSeq) return
    // The edit reached the server (setTarget above succeeded), but the read
    // that would confirm the new number failed. Two things are known for
    // certain here: the server holds a newer target than we do, and we do
    // not know what it is — the same state as "never learned a value", so
    // it gets the same treatment: null, not a guess of `next` and not the
    // stale pre-edit number. That is what keeps start() from sending a body
    // and overwriting the edit that already took effect server-side.
    // [TH] การแก้ไขถึงเซิร์ฟเวอร์แล้ว (setTarget ข้างบนสำเร็จ) แต่การอ่านยืนยันค่าใหม่ล้มเหลว
    //      สิ่งที่รู้แน่ๆ คือเซิร์ฟเวอร์มีค่าใหม่กว่าที่เรามี และเราไม่รู้ว่าเป็นค่าอะไร เหมือนกับ
    //      "ยังไม่เคยรู้ค่าเลย" จึงจัดการแบบเดียวกัน คือเป็น null ไม่เดาว่าเป็น next และไม่ใช้ค่าเก่า
    //      ก่อนแก้ไข นี่คือสิ่งที่ทำให้ start() ไม่ส่งค่านี้ไปทับการแก้ไขที่สำเร็จแล้วที่เซิร์ฟเวอร์
    target.value = null
    targetUnconfirmed.value = true
  }
}

async function start() {
  errorMsg.value = ''
  if (!serverUrl.value) {
    errorMsg.value = 'ยังไม่ได้ตั้งค่าที่อยู่เซิร์ฟเวอร์ (ให้เจ้าหน้าที่ตั้งค่าในเมนูตั้งค่า)'
    return
  }
  if (!deviceCode.value) {
    errorMsg.value = 'ยังไม่ได้ตั้งรหัสเครื่อง (ให้เจ้าหน้าที่ตั้งค่าในเมนูตั้งค่า)'
    return
  }
  phase.value = 'starting'
  attachAvailable.value = false
  try {
    // An unlearned target sends no body: device.ts leaves whatever the machine
    // already holds untouched, rather than us guessing and overwriting it.
    // [TH] ถ้ายังไม่รู้ค่าเป้าหมายจะไม่ส่งค่านี้ไปเลย เครื่องจะใช้ค่าที่มันถืออยู่เดิม
    //      แทนที่จะให้แอปเดาแล้วไปเขียนทับ
    await queueStart(serverUrl.value, deviceCode.value, target.value ?? undefined)
    if (disposed) return
    startPolling()
  } catch (e) {
    if (disposed) return
    phase.value = 'before'
    if (e instanceof DeviceError && e.kind === 'busy') {
      errorMsg.value = 'เครื่องกำลังทำงานอยู่แล้ว'
      attachAvailable.value = true
    } else if (e instanceof DeviceError && e.kind === 'no-device') {
      errorMsg.value = 'ยังไม่ได้ตั้งรหัสเครื่อง (ให้เจ้าหน้าที่ตั้งค่าในเมนูตั้งค่า)'
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
    await queueStop(serverUrl.value, deviceCode.value)
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
  if (!serverUrl.value || !deviceCode.value) return
  try {
    const cur = await getCurrent(serverUrl.value, deviceCode.value)
    if (disposed) return
    // Learn the machine's real target before the user ever presses start, so
    // the pre-start stepper shows the truth and start() has a real value to
    // carry through instead of guessing.
    // [TH] เรียนรู้ค่าเป้าหมายจริงของเครื่องก่อนที่ผู้ใช้จะกดเริ่ม ตัวเลขบนหน้าจอก่อนเริ่ม
    //      จะได้ตรงกับความจริง และตอนกดเริ่มก็มีค่าจริงให้ส่งไป ไม่ต้องเดา
    target.value = cur.targetReps
    targetUnconfirmed.value = false
    if (cur.state === 'running' && phase.value === 'before') attachAvailable.value = true
  } catch {
    // probe is best-effort; start() reports connection errors. target stays
    // null (unknown) — the stepper is disabled and start() sends no body.
    // [TH] การตรวจสอบครั้งนี้พลาดได้ไม่เป็นไร start() จะแจ้งปัญหาการเชื่อมต่อเอง
    //      ค่าเป้าหมายจะยังคงเป็น null (ไม่ทราบ) ปุ่มปรับจะถูกปิด และ start() จะไม่ส่งค่านี้ไป
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
      <div class="target-stepper">
        <button
          class="step"
          aria-label="ลดจำนวนเป้าหมาย"
          :disabled="target === null || target === 0"
          @click="stepTarget(-1)"
        >
          −
        </button>
        <span class="target-label">เป้าหมาย {{ targetLabel }}</span>
        <button
          class="step"
          aria-label="เพิ่มจำนวนเป้าหมาย"
          :disabled="target === null || target === 99"
          @click="stepTarget(1)"
        >
          +
        </button>
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
        จำนวนรอบ:
        <Transition name="pop" mode="out-in">
          <span :key="reps" class="reps-num">{{ reps }}</span>
        </Transition>
        <span v-if="target !== null && target > 0"> / {{ target }}</span>
      </p>
      <div class="target-stepper">
        <button
          class="step"
          aria-label="ลดจำนวนเป้าหมาย"
          :disabled="target === null || target === 0"
          @click="stepTarget(-1)"
        >
          −
        </button>
        <span class="target-label">เป้าหมาย {{ targetLabel }}</span>
        <button
          class="step"
          aria-label="เพิ่มจำนวนเป้าหมาย"
          :disabled="target === null || target === 99"
          @click="stepTarget(1)"
        >
          +
        </button>
      </div>
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

.target-stepper {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.75rem;
  margin-bottom: 1rem;
}

.target-label {
  min-width: 8rem;
  text-align: center;
  font-weight: 600;
}

.step {
  min-width: 3rem;
  font-size: 1.3rem;
  padding: 0.5rem;
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
