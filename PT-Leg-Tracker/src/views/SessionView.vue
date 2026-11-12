<script setup lang="ts">
// หน้าออกกำลังกาย: สั่งเริ่ม/หยุดเครื่องผ่านเซิร์ฟเวอร์ แสดงเวลา/จำนวนรอบสด บันทึกความปวดก่อน-หลัง
// Exercise screen: start/stop the device via the server, live timer/reps, before/after pain logging.
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import AppIcon from '@/components/AppIcon.vue'
import PainScale from '@/components/PainScale.vue'
import ConfettiBurst from '@/components/ConfettiBurst.vue'
import TargetStepper from '@/components/TargetStepper.vue'
import { flushClaims, queueClaim } from '@/lib/claims'
import { DeviceError, getCurrent, queueStart, queueStop, setTarget } from '@/lib/device'
import { canOpenConfirm, confirmSeed, planConfirmStep } from '@/lib/start-confirm'
import { performTargetStep, planTargetStep } from '@/lib/target-step'
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
// The value the latest tap asked for, while its request is still in flight. Taps
// accumulate on this rather than on `target`, which only ever holds a number the
// server confirmed. Without it each tap computed its next value from the last
// CONFIRMED number, so three quick taps on 10 all computed 11 and the user got
// +1 for three presses with nothing to show the other two were dropped.
// [TH] ค่าที่การกดครั้งล่าสุดขอไว้ ระหว่างที่คำขอยังไม่ได้คำตอบ
//      การกดจะสะสมบนค่านี้ ไม่ใช่บน target ซึ่งเก็บเฉพาะตัวเลขที่เซิร์ฟเวอร์ยืนยันแล้ว
//      ถ้าไม่มีค่านี้ การกดแต่ละครั้งจะคำนวณค่าถัดไปจากตัวเลขที่ยืนยันล่าสุด
//      การกดเร็วๆ สามครั้งบนค่า 10 จึงคำนวณได้ 11 ทั้งสามครั้ง ผู้ใช้กดสามทีแต่ได้เพิ่มแค่หนึ่ง
//      โดยไม่มีอะไรบอกว่าอีกสองครั้งหายไป
const targetPending = ref<number | null>(null)
// What the stepper reads and writes: the pending value while one is outstanding,
// the confirmed one otherwise.
// [TH] ค่าที่ปุ่มปรับใช้อ่านและเขียน คือค่าที่ค้างอยู่ถ้ามี ถ้าไม่มีก็ใช้ค่าที่ยืนยันแล้ว
const targetShown = computed(() => targetPending.value ?? target.value)
const targetLabel = computed(() => {
  if (targetShown.value === null)
    return targetUnconfirmed.value ? 'ส่งค่าไปแล้ว แต่ยืนยันค่าจริงไม่ได้' : 'ไม่ทราบค่า'
  return targetShown.value > 0 ? `${targetShown.value} ครั้ง` : 'ไม่จำกัด'
})
const attachAvailable = ref(false)
const connLost = ref(false)
const errorMsg = ref('')

// The pre-start confirmation sheet: open/closed, and the set count it is
// currently showing while open. This value only ever reaches queueStart — it
// is never written through setTarget — so a one-off choice here never becomes
// the machine's stored default. 0 is a legal choice ("run until STOP"), so
// confirmValue is a plain number, never null.
// [TH] แผ่นยืนยันก่อนเริ่ม: เปิด/ปิด และจำนวนที่กำลังแสดงอยู่ขณะเปิด ค่านี้จะไปถึงแค่ queueStart เท่านั้น
//      ไม่เคยเขียนผ่าน setTarget ทางเลือกครั้งเดียวตรงนี้จึงไม่มีวันกลายเป็นค่าเริ่มต้นที่เครื่องจำไว้
//      0 เป็นค่าที่เลือกได้จริง ("ทำไปเรื่อยๆ จนกว่าจะกด STOP") confirmValue จึงเป็นตัวเลขล้วนๆ ไม่มี null
const confirmOpen = ref(false)
const confirmValue = ref(0)
const confirmLabel = computed(() =>
  confirmValue.value > 0 ? `${confirmValue.value} ครั้ง` : 'ไม่จำกัด',
)

let poller: ReturnType<typeof setTimeout> | null = null
let disposed = false
// Bumped by stopPolling so a poll still in flight when polling stops (stop(),
// failOffline) cannot land afterwards and flip the phase back to 'running'.
// [TH] เพิ่มค่าทุกครั้งที่ stopPolling ผลของการอ่านสถานะที่ค้างอยู่ตอนหยุดจะได้ไม่ย้อนกลับมาเปลี่ยนหน้าจอ
let pollGen = 0
// Consecutive failed polls while still 'starting'; the server dropped after
// accepting the start, and without a cap the screen would wait forever.
// [TH] จำนวนครั้งที่อ่านสถานะพลาดติดกันระหว่างรอเครื่องเริ่ม ถ้าไม่จำกัด หน้าจอจะรอไปตลอด
let startingFails = 0
const STARTING_MAX_FAILS = 5 // 5 polls × 2 s = 10 s
const saving = ref(false)

const serverUrl = computed(() => settings.settings.serverUrl.trim())
const deviceCode = computed(() => settings.settings.deviceCode.trim())

const clock = computed(() => {
  if (elapsedSec.value === null) return '--:--'
  const m = String(Math.floor(elapsedSec.value / 60)).padStart(2, '0')
  const s = String(elapsedSec.value % 60).padStart(2, '0')
  return `${m}:${s}`
})

// How full the running ring is: reps against the target, or a full ring when
// the session has no limit (or the target is unknown).
// [TH] วงแหวนเต็มแค่ไหน: จำนวนรอบเทียบกับเป้าหมาย ถ้าไม่จำกัดหรือไม่ทราบค่า วงจะเต็มวง
const ringPct = computed(() => {
  const t = targetShown.value
  return t !== null && t > 0 ? Math.min(100, (reps.value / t) * 100) : 100
})

function stopPolling() {
  pollGen++
  if (poller) clearTimeout(poller)
  poller = null
}

function startPolling() {
  stopPolling()
  startingFails = 0
  const gen = pollGen
  // Run one request at a time. setInterval allowed a slow older response to
  // land after a newer one and made the clock jump backwards (23, 21, 22...).
  // The device/server value is authoritative; the app does not run a second
  // competing session clock between responses.
  // [TH] อ่านทีละคำขอเท่านั้น เดิม setInterval ทำให้คำตอบเก่าที่ช้ากว่ามาถึงหลังคำตอบใหม่
  //      จนเวลาเด้งถอยหลัง แอปจึงใช้เวลาจากเครื่อง/เซิร์ฟเวอร์โดยตรง ไม่จับเวลาแข่งเอง
  void poll(gen)
}

async function poll(gen = pollGen) {
  try {
    const cur = await getCurrent(serverUrl.value, deviceCode.value)
    if (disposed || gen !== pollGen) return
    connLost.value = false
    startingFails = 0
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
    if (disposed || gen !== pollGen) return
    connLost.value = true // keep last-known values; keep polling
    if (phase.value === 'starting' && ++startingFails >= STARTING_MAX_FAILS) {
      // If the device did start, the next start() gets 'busy' and offers attach.
      // [TH] ถ้าเครื่องเริ่มไปแล้วจริง การกดเริ่มครั้งถัดไปจะได้ busy และมีปุ่มเข้าร่วมให้
      stopPolling()
      phase.value = 'before'
      errorMsg.value = 'เชื่อมต่อไม่สำเร็จ ลองใหม่ภายหลัง'
    }
  } finally {
    if (!disposed && gen === pollGen && (phase.value === 'starting' || phase.value === 'running')) {
      poller = setTimeout(() => void poll(gen), 1000)
    }
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
  // The decision branching (setTarget success/fail × re-read success/fail × a stale tap)
  // lives in src/lib/target-step.ts, where it is covered by tests that do not need to mount
  // this component. This function only owns the refs: it plans the step, shows the pending
  // value immediately, and applies whatever outcome the module reports.
  // [TH] เงื่อนไขการตัดสินใจ (setTarget สำเร็จ/ล้มเหลว × อ่านยืนยันสำเร็จ/ล้มเหลว × การกดที่ค้างเก่า)
  //      อยู่ใน src/lib/target-step.ts ซึ่งมีชุดทดสอบครอบคลุมโดยไม่ต้อง mount คอมโพเนนต์นี้
  //      ฟังก์ชันนี้ดูแลแค่ ref: วางแผนค่าที่จะขอ แสดงค่าที่ค้างอยู่ทันที แล้วนำผลลัพธ์ที่โมดูล
  //      รายงานกลับมาไปใช้
  const plan = planTargetStep(targetShown.value, delta)
  if (!plan) return
  const opSeq = ++targetOpSeq
  targetPending.value = plan.next
  const outcome = await performTargetStep(plan.next, {
    setTarget: (reps) => setTarget(serverUrl.value, deviceCode.value, reps),
    getCurrent: () => getCurrent(serverUrl.value, deviceCode.value),
    isCurrent: () => opSeq === targetOpSeq,
    isDisposed: () => disposed,
  })
  switch (outcome.kind) {
    case 'stale':
      return
    case 'failed':
      targetPending.value = null
      return
    case 'confirmed':
      target.value = outcome.targetReps
      targetPending.value = null
      targetUnconfirmed.value = false
      return
    case 'unconfirmed':
      target.value = null
      targetPending.value = null
      targetUnconfirmed.value = true
      return
    default: {
      // All TargetStepOutcome kinds must be handled here; adding a new kind to the union
      // will cause a compile error at this line, which is intentional.
      // [TH] ต้องจัดการกับ TargetStepOutcome ทุกชนิดที่นี่ ถ้าเพิ่มชนิดใหม่เข้ามา
      //      คอมไพเลอร์จะหยุดที่บรรทัดนี้ ซึ่งเป็นเจตนาโดยมีจุดประสงค์
      const _exhaustive: never = outcome
      return _exhaustive
    }
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
  // Ask how many sets to run before committing — but only when `target` is a
  // baseline the sheet can seed itself with honestly. canOpenConfirm says no
  // while a stepTarget() edit is still in flight, or before any value has ever
  // been learned; in both cases we fall through to the pre-Task-10 behaviour
  // below and start with no body, exactly as before this sheet existed.
  // [TH] ถามจำนวนเซ็ตก่อนเริ่มจริง แต่จะเปิดแผ่นยืนยันก็ต่อเมื่อ target ตอนนี้เป็นค่าตั้งต้นที่เชื่อถือได้เท่านั้น
  //      canOpenConfirm จะตอบว่าไม่ได้ ถ้าการแก้ไขจาก stepTarget() ยังค้างอยู่ หรือยังไม่เคยรู้ค่าเลย
  //      ทั้งสองกรณีจะข้ามไปใช้พฤติกรรมเดิมก่อน Task 10 ด้านล่าง คือเริ่มโดยไม่ส่งค่าไปเลย เหมือนก่อนมีแผ่นนี้
  if (canOpenConfirm(target.value, targetPending.value)) {
    confirmValue.value = confirmSeed(target.value)
    confirmOpen.value = true
    return
  }
  // An unlearned target sends no body: device.ts leaves whatever the machine
  // already holds untouched, rather than us guessing and overwriting it. The
  // same applies while a stepTarget() edit is still in flight (targetPending
  // non-null): targetShown may already read ahead of `target` from taps the
  // server has not confirmed yet, and `target` itself is stale until it does.
  // Sending it here would win the race against that outstanding setTarget and
  // silently revert the in-flight edit the moment the device polls this start.
  // [TH] ถ้ายังไม่รู้ค่าเป้าหมายจะไม่ส่งค่านี้ไปเลย เครื่องจะใช้ค่าที่มันถืออยู่เดิม
  //      แทนที่จะให้แอปเดาแล้วไปเขียนทับ กรณีที่การแก้ไขจาก stepTarget() ยังค้างอยู่ก็เช่นกัน
  //      (targetPending ไม่เป็น null): ค่าที่หน้าจอแสดง (targetShown) อาจนำหน้า target ไปแล้ว
  //      จากการกดที่เซิร์ฟเวอร์ยังไม่ยืนยัน และ target เองก็เป็นค่าเก่าจนกว่าจะยืนยันเสร็จ
  //      ถ้าส่งค่านี้ไปตอนนี้จะไปแข่งกับ setTarget ที่ยังค้างอยู่ และอาจทับการแก้ไขที่ค้างอยู่นั้น
  //      เงียบๆ ทันทีที่เครื่องโพลคำสั่งเริ่มนี้
  await commitStart(undefined)
}

// Shared by both paths that actually queue a start: the direct path above
// (canOpenConfirm said no — no sheet, no target) and confirmStart() below (the
// sheet's ตกลง). Only the sheet ever supplies a defined number, and it goes
// straight to queueStart — never to setTarget — so the choice stays scoped to
// this one session.
// [TH] ใช้ร่วมกันโดยสองทางที่เข้าคิวเริ่มจริง: ทางตรงด้านบน (canOpenConfirm ตอบว่าไม่ได้ — ไม่มีแผ่นยืนยัน
//      ไม่มีค่าเป้าหมาย) และ confirmStart() ด้านล่าง (การกดตกลงบนแผ่นยืนยัน) มีแต่แผ่นยืนยันเท่านั้นที่ส่งตัวเลขจริงมา
//      และไปที่ queueStart ตรงๆ ไม่ผ่าน setTarget เลย ทางเลือกจึงมีผลแค่เซสชันนี้เซสชันเดียว
async function commitStart(targetReps: number | undefined) {
  phase.value = 'starting'
  attachAvailable.value = false
  try {
    await queueStart(serverUrl.value, deviceCode.value, targetReps)
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

function stepConfirm(delta: number) {
  confirmValue.value = planConfirmStep(confirmValue.value, delta)
}

async function confirmStart() {
  confirmOpen.value = false
  await commitStart(confirmValue.value)
}

function cancelConfirm() {
  confirmOpen.value = false
  phase.value = 'before'
}

function attach() {
  errorMsg.value = ''
  attachAvailable.value = false
  phase.value = 'running'
  startPolling()
}

async function stop() {
  // attach() shows the stop button before the first poll has learned the
  // session id; without it the session would never be claimed for this patient.
  // [TH] attach() แสดงปุ่มหยุดก่อนที่จะรู้รหัสเซสชัน ถ้าไม่ถามก่อน เซสชันนี้จะไม่ถูกผูกกับผู้ป่วย
  if (!sessionId.value) {
    try {
      sessionId.value = (await getCurrent(serverUrl.value, deviceCode.value)).sessionId
    } catch {
      // unreachable: stop below fails the same way and the flow still advances
    }
  }
  try {
    await queueStop(serverUrl.value, deviceCode.value)
  } catch {
    // session already over or server unreachable — flow advances regardless
  }
  stopPolling()
  phase.value = 'after'
}

async function save() {
  if (painBefore.value === null || painAfter.value === null || saving.value) return
  saving.value = true
  try {
    await painLogStore.add({
      sessionId: sessionId.value,
      recordedAt: new Date().toISOString(),
      painBefore: painBefore.value,
      painAfter: painAfter.value,
    })
    // Queued first so a failed claim is retried by the next sync instead of lost.
    // [TH] เข้าคิวก่อน ถ้าผูกไม่สำเร็จ การซิงก์ครั้งถัดไปจะลองใหม่ ไม่หายไปเฉยๆ
    const code = profileStore.profile?.patientCode
    if (sessionId.value && code) await queueClaim(sessionId.value, code)
    if (serverUrl.value) {
      try {
        await flushClaims(serverUrl.value)
      } catch {
        // best-effort: anything left stays queued, local log is saved
      }
    }
  } catch {
    saving.value = false
    errorMsg.value = 'บันทึกไม่สำเร็จ กรุณาลองใหม่'
    return
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
      <template v-if="confirmOpen">
        <div class="card">
          <h2>รอยืนยัน</h2>
          <TargetStepper
            :value="confirmValue"
            :label="confirmLabel"
            prefix=""
            noun="เซ็ต"
            @step="stepConfirm"
          />
        </div>
        <button class="primary wide" @click="confirmStart"><AppIcon name="check" /> ตกลง</button>
        <button class="secondary wide" @click="cancelConfirm">ยกเลิก</button>
      </template>
      <template v-else>
        <div class="card">
          <h2>ก่อนเริ่ม ปวดเข่าแค่ไหน?</h2>
          <PainScale v-model="painBefore" />
        </div>
        <TargetStepper :value="targetShown" :label="targetLabel" @step="stepTarget" />
        <p class="card">
          สวมอุปกรณ์ให้เรียบร้อย (ดูวิธีใช้ได้ที่เมนู "วิธีใช้เครื่อง") แล้วกดเริ่ม
        </p>
        <p v-if="phase === 'starting' && connLost" class="hint">กำลังเชื่อมต่อ…</p>
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
    </template>

    <template v-else-if="phase === 'running'">
      <div class="clock-ring">
        <svg viewBox="0 0 120 120" aria-hidden="true">
          <circle class="track" cx="60" cy="60" r="54" />
          <circle
            class="arc"
            cx="60"
            cy="60"
            r="54"
            pathLength="100"
            :stroke-dasharray="`${ringPct} 100`"
          />
        </svg>
        <p class="clock">{{ clock }}</p>
      </div>
      <p class="reps">
        จำนวนรอบ:
        <Transition name="pop" mode="out-in">
          <span :key="reps" class="reps-num">{{ reps }}</span>
        </Transition>
        <span v-if="targetShown !== null && targetShown > 0"> / {{ targetShown }}</span>
      </p>
      <TargetStepper :value="targetShown" :label="targetLabel" @step="stepTarget" />
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
      <p v-if="errorMsg" class="error">{{ errorMsg }}</p>
      <button class="primary wide" :disabled="painAfter === null || saving" @click="save">
        <AppIcon name="check" /> บันทึกผล
      </button>
    </template>
  </main>
</template>

<style scoped>
/* [TH] วงแหวนความคืบหน้า: เส้นสีเขียวเติมตามจำนวนรอบที่ทำได้ เวลาอยู่ตรงกลาง */
/* Progress ring: the teal arc fills with reps done; the clock sits in the middle. */
.clock-ring {
  position: relative;
  width: min(15rem, 80vw);
  aspect-ratio: 1;
  margin: 1.5rem auto 1rem;
  display: flex;
  align-items: center;
  justify-content: center;
}

.clock-ring svg {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  transform: rotate(-90deg);
}

.clock-ring circle {
  fill: none;
  stroke-width: 9;
}

.track {
  stroke: var(--c-primary-wash);
}

.arc {
  stroke: var(--c-primary);
  stroke-linecap: round;
}

.clock {
  position: relative;
  font-size: 3.25rem;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
  line-height: 1;
}

.reps-num {
  display: inline-block;
  font-weight: 700;
  color: var(--c-primary);
}

@media (prefers-reduced-motion: no-preference) {
  .arc {
    transition: stroke-dasharray 0.6s ease-out;
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
  color: var(--c-muted);
  margin-bottom: 1.5rem;
}

.error {
  margin-bottom: 0.75rem;
}

.wide {
  width: 100%;
  font-size: 1.2rem;
  min-height: 4rem;
  margin-bottom: 0.625rem;
}
</style>
