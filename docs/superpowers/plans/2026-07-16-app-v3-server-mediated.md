# App v3 — Server-Mediated Device Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rework the app's device control to talk to the FastAPI server (Plan 1, already shipped) instead of the ESP32 directly: client rewrite, SessionView attach flow + reps display, Settings device-address removal, records-sync endpoint switch, mock script retirement.

**Architecture:** The server owns all session state (`GET /api/sessions/current` is the live view; the app never times sessions). The app queues start/stop commands (202-accepted, asynchronous), polls `current` every 2 s, and claims finished sessions with the patient code after the post-pain save. Spec: `docs/superpowers/specs/2026-07-13-server-mediated-device-control-design.md`.

**Tech Stack:** Vue 3 + TypeScript + Vite, Pinia, vitest. Server (already built): FastAPI + SQLite in `server/`.

## Global Constraints

- All npm/npx commands run from `PT-Leg-Tracker/` (repo-root subdirectory), not the repo root.
- Typed-error style: UI maps `DeviceError.kind` → Thai message; **never** show raw `.message` / English to the user.
- User-facing copy is Thai. Exact spec strings: server unreachable → `เชื่อมต่อไม่สำเร็จ ลองใหม่ภายหลัง`; device offline → `เครื่องออฟไลน์`; 409 on start → `เครื่องกำลังทำงานอยู่แล้ว`; mid-session poll failure → keep last-known values + `กำลังเชื่อมต่อ…`.
- The app never times sessions locally — display server-reported `elapsedSec`/`reps` only.
- 2 s poll cadence in SessionView (unchanged from v2).
- Client request timeout stays 5000 ms.
- Each task must leave `npm run test` and `npm run build` green (no half-migrated commits).
- No `Co-Authored-By` / AI attribution in commit messages.

**Task order matters:** Task 1 adds the new client *alongside* the v2 functions; Task 3 (SessionView) is the only consumer of the v2 functions and deletes them when it switches over; Task 4 removes `Settings.deviceUrl` only after nothing reads it.

---

### Task 1: Server-session client (`device.ts` additions)

**Files:**
- Modify: `PT-Leg-Tracker/src/types.ts` (add `CurrentSession`; keep `DeviceStatus` for now — Task 3 removes it)
- Modify: `PT-Leg-Tracker/src/lib/device.ts` (add new functions; keep `startSession`/`getStatus`/`stopSession` for now — Task 3 removes them)
- Test: `PT-Leg-Tracker/src/lib/device.test.ts` (append new describes; existing v2 tests stay until Task 3)

**Interfaces:**
- Consumes: existing `DeviceError`, `request()` helper, `base()` helper in `device.ts`.
- Produces (used by Tasks 3):
  - `type DeviceErrorKind = 'unreachable' | 'busy' | 'idle' | 'offline'` (adds `'offline'`)
  - `interface CurrentSession { sessionId: string | null; state: 'idle' | 'starting' | 'running'; elapsedSec: number; reps: number; deviceOnline: boolean }` (in `types.ts`)
  - `queueStart(serverUrl: string): Promise<void>` — `POST {base}/api/sessions/start`; 409 → `DeviceError('busy')`
  - `queueStop(serverUrl: string): Promise<void>` — `POST {base}/api/sessions/stop`; 409 → `DeviceError('idle')`
  - `getCurrent(serverUrl: string): Promise<CurrentSession>` — `GET {base}/api/sessions/current`; malformed → `DeviceError('unreachable')`
  - `claimSession(serverUrl: string, sessionId: string, patientCode: string): Promise<void>` — `POST {base}/api/sessions/{id}/claim` with JSON body `{patientCode}`; any failure (incl. 404) → `DeviceError('unreachable')`

- [ ] **Step 1: Write the failing tests** — append to `PT-Leg-Tracker/src/lib/device.test.ts` (reuses the file's existing `mockFetchOnce`/`jsonRes` helpers):

```typescript
import { claimSession, getCurrent, queueStart, queueStop } from './device'

describe('queueStart', () => {
  it('POSTs to /api/sessions/start and resolves on 202', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await expect(queueStart('http://srv/')).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/start',
      expect.objectContaining({ method: 'POST' }),
    )
  })
  it('throws busy on 409', async () => {
    mockFetchOnce(jsonRes(409, { detail: 'session already running' }))
    await expect(queueStart('http://srv')).rejects.toMatchObject({ kind: 'busy' })
  })
  it('throws unreachable on network error', async () => {
    mockFetchOnce(new TypeError('failed'))
    await expect(queueStart('http://srv')).rejects.toMatchObject({ kind: 'unreachable' })
  })
})

describe('queueStop', () => {
  it('POSTs to /api/sessions/stop and resolves on 202', async () => {
    const fn = mockFetchOnce(jsonRes(202, { queued: true }))
    await expect(queueStop('http://srv')).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/stop',
      expect.objectContaining({ method: 'POST' }),
    )
  })
  it('throws idle on 409', async () => {
    mockFetchOnce(jsonRes(409, { detail: 'no session running' }))
    await expect(queueStop('http://srv')).rejects.toMatchObject({ kind: 'idle' })
  })
})

describe('getCurrent', () => {
  it('parses a running view', async () => {
    mockFetchOnce(
      jsonRes(200, {
        sessionId: 's1',
        state: 'running',
        elapsedSec: 42,
        reps: 7,
        deviceOnline: true,
      }),
    )
    await expect(getCurrent('http://srv')).resolves.toEqual({
      sessionId: 's1',
      state: 'running',
      elapsedSec: 42,
      reps: 7,
      deviceOnline: true,
    })
  })
  it('parses a starting view with null sessionId', async () => {
    mockFetchOnce(
      jsonRes(200, {
        sessionId: null,
        state: 'starting',
        elapsedSec: 0,
        reps: 0,
        deviceOnline: false,
      }),
    )
    await expect(getCurrent('http://srv')).resolves.toEqual({
      sessionId: null,
      state: 'starting',
      elapsedSec: 0,
      reps: 0,
      deviceOnline: false,
    })
  })
  it('throws unreachable on malformed state', async () => {
    mockFetchOnce(jsonRes(200, { state: 'weird' }))
    await expect(getCurrent('http://srv')).rejects.toMatchObject({ kind: 'unreachable' })
  })
})

describe('claimSession', () => {
  it('POSTs patientCode as JSON to the claim endpoint', async () => {
    const fn = mockFetchOnce(jsonRes(200, { ok: true }))
    await expect(claimSession('http://srv/', 's1', 'P001')).resolves.toBeUndefined()
    expect(fn).toHaveBeenCalledWith(
      'http://srv/api/sessions/s1/claim',
      expect.objectContaining({
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ patientCode: 'P001' }),
      }),
    )
  })
  it('throws unreachable on 404 (unknown session)', async () => {
    mockFetchOnce(jsonRes(404, { detail: 'unknown session' }))
    await expect(claimSession('http://srv', 'nope', 'P001')).rejects.toMatchObject({
      kind: 'unreachable',
    })
  })
})

describe('DeviceError offline kind', () => {
  it('accepts the offline kind', () => {
    expect(new DeviceError('offline').kind).toBe('offline')
  })
})
```

Merge the import line with the file's existing `./device` import (one import statement total).

- [ ] **Step 2: Run tests to verify they fail**

Run (from `PT-Leg-Tracker/`): `npm run test`
Expected: FAIL — `queueStart` etc. not exported.

- [ ] **Step 3: Implement** — in `PT-Leg-Tracker/src/types.ts`, add below `DeviceStatus`:

```typescript
export interface CurrentSession {
  sessionId: string | null
  state: 'idle' | 'starting' | 'running'
  elapsedSec: number
  reps: number
  deviceOnline: boolean
}
```

In `PT-Leg-Tracker/src/lib/device.ts`: change the kind union and import, then append the four functions.

```typescript
import type { CurrentSession, DeviceStatus } from '@/types'

export type DeviceErrorKind = 'unreachable' | 'busy' | 'idle' | 'offline'
```

```typescript
// --- v3 server-session client (spec: server-mediated device control) ---

export async function queueStart(serverUrl: string): Promise<void> {
  await request(`${base(serverUrl)}/api/sessions/start`, 'busy', { method: 'POST' })
}

export async function queueStop(serverUrl: string): Promise<void> {
  await request(`${base(serverUrl)}/api/sessions/stop`, 'idle', { method: 'POST' })
}

export async function getCurrent(serverUrl: string): Promise<CurrentSession> {
  const d = (await request(`${base(serverUrl)}/api/sessions/current`, 'unreachable')) as Record<
    string,
    unknown
  >
  if (d.state !== 'idle' && d.state !== 'starting' && d.state !== 'running')
    throw new DeviceError('unreachable')
  return {
    sessionId: typeof d.sessionId === 'string' ? d.sessionId : null,
    state: d.state,
    elapsedSec: typeof d.elapsedSec === 'number' ? d.elapsedSec : 0,
    reps: typeof d.reps === 'number' ? d.reps : 0,
    deviceOnline: d.deviceOnline === true,
  }
}

export async function claimSession(
  serverUrl: string,
  sessionId: string,
  patientCode: string,
): Promise<void> {
  await request(`${base(serverUrl)}/api/sessions/${encodeURIComponent(sessionId)}/claim`, 'unreachable', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ patientCode }),
  })
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `npm run test`
Expected: PASS (all files, old v2 device tests included).

- [ ] **Step 5: Build + commit**

Run: `npm run build` — expected: clean.

```bash
git add src/types.ts src/lib/device.ts src/lib/device.test.ts
git commit -m "feat: server-session client alongside v2 device client"
```

---

### Task 2: Records sync switch (`api.ts`)

**Files:**
- Modify: `PT-Leg-Tracker/src/lib/api.ts` (rewrite `fetchDeviceSessions`; `syncNow` unchanged)
- Test: `PT-Leg-Tracker/src/lib/api.test.ts` (create)

**Interfaces:**
- Consumes: `Session` type (`source: 'device'`), server endpoint `GET /api/sessions?patientCode=X` → `[{id, startedAt, endedAt, durationSec, reps, patientCode}]` (finished sessions only; `durationSec` set on close but typed nullable defensively).
- Produces: `fetchDeviceSessions(serverUrl: string, patientCode: string): Promise<Session[]>` — same signature as v2, new URL + field mapping (`startedAt` → `date`). `syncNow()` and `mergeSessions` untouched.

- [ ] **Step 1: Write the failing test** — create `PT-Leg-Tracker/src/lib/api.test.ts`:

```typescript
import { afterEach, describe, expect, it, vi } from 'vitest'
import { fetchDeviceSessions } from './api'

afterEach(() => vi.unstubAllGlobals())

describe('fetchDeviceSessions', () => {
  it('queries /api/sessions and maps server fields to Session', async () => {
    const fn = vi.fn().mockResolvedValueOnce({
      ok: true,
      status: 200,
      json: async () => [
        {
          id: 's1',
          startedAt: '2026-07-16T01:00:00+00:00',
          endedAt: '2026-07-16T01:10:00+00:00',
          durationSec: 600,
          reps: 12,
          patientCode: 'P001',
        },
      ],
    })
    vi.stubGlobal('fetch', fn)
    await expect(fetchDeviceSessions('http://srv/', 'P001')).resolves.toEqual([
      { id: 's1', date: '2026-07-16T01:00:00+00:00', durationSec: 600, source: 'device' },
    ])
    expect(fn).toHaveBeenCalledWith('http://srv/api/sessions?patientCode=P001')
  })
  it('URL-encodes the patient code', async () => {
    const fn = vi.fn().mockResolvedValueOnce({ ok: true, status: 200, json: async () => [] })
    vi.stubGlobal('fetch', fn)
    await fetchDeviceSessions('http://srv', 'a b')
    expect(fn).toHaveBeenCalledWith('http://srv/api/sessions?patientCode=a%20b')
  })
  it('throws on HTTP error', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValueOnce({ ok: false, status: 500 }))
    await expect(fetchDeviceSessions('http://srv', 'P001')).rejects.toThrow('HTTP 500')
  })
})
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npm run test`
Expected: FAIL — old code calls `/api/patients/P001/sessions` and expects `date` field.

- [ ] **Step 3: Implement** — in `PT-Leg-Tracker/src/lib/api.ts`, replace the `RemoteSession` interface and `fetchDeviceSessions`:

```typescript
interface RemoteSession {
  id: string
  startedAt: string
  endedAt: string | null
  durationSec: number | null
  reps: number
  patientCode: string | null
}

export async function fetchDeviceSessions(
  serverUrl: string,
  patientCode: string,
): Promise<Session[]> {
  const base = serverUrl.replace(/\/+$/, '')
  const res = await fetch(`${base}/api/sessions?patientCode=${encodeURIComponent(patientCode)}`)
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  const list = (await res.json()) as RemoteSession[]
  return list.map((r) => ({
    id: r.id,
    date: r.startedAt,
    durationSec: r.durationSec ?? 0,
    source: 'device' as const,
  }))
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `npm run test`
Expected: PASS.

- [ ] **Step 5: Build + commit**

Run: `npm run build` — expected: clean.

```bash
git add src/lib/api.ts src/lib/api.test.ts
git commit -m "feat: records sync via server sessions endpoint"
```

---

### Task 3: SessionView v3 — starting flow, attach, reps, claim

**Files:**
- Modify: `PT-Leg-Tracker/src/views/SessionView.vue` (full script + template rework below)
- Modify: `PT-Leg-Tracker/src/lib/device.ts` (delete v2 `startSession`, `getStatus`, `stopSession` — now unused)
- Modify: `PT-Leg-Tracker/src/lib/device.test.ts` (delete the v2 `startSession`/`getStatus`/`stopSession` describes; **keep** the `request timeout` describe but rewrite it to call `getCurrent` — it covers the shared `request()` helper; expected URL becomes `http://dev/api/sessions/current`)
- Modify: `PT-Leg-Tracker/src/types.ts` (delete `DeviceStatus` — last consumer gone)

**Interfaces:**
- Consumes: `queueStart`/`queueStop`/`getCurrent`/`claimSession`/`DeviceError` (Task 1), `useSettingsStore().settings.serverUrl`, `useProfileStore().profile?.patientCode`, `usePainLogStore().add`, `PainScale`, `AppIcon`.
- Produces: nothing consumed by later tasks. After this task nothing in `src/` reads `settings.deviceUrl` (Task 4 relies on that).

No component-test framework exists; this task is verified by the vitest suite staying green, `npm run build`, and the manual end-to-end check in Task 5.

Behavior spec (from design doc):
- **Mount probe:** if `current` reports `running`, offer attach instead of only start.
- **Start:** `queueStart` → 202 means *queued*, not running. Enter `starting` phase and poll `current` every 2 s. `starting` → wait; `running` → running phase; `starting` with `deviceOnline: false` **or** regression to `idle` (command expired) → back to `before` with `เครื่องออฟไลน์`. Worst-case visible-running latency ≈ 3–4 s.
- **409 on start:** `เครื่องกำลังทำงานอยู่แล้ว` + offer attach.
- **Running:** display server `elapsedSec` + `reps`; poll failure → keep last-known values + `กำลังเชื่อมต่อ…`; `idle` → session ended → after phase.
- **Attach:** requires painBefore (same gate as start); joins the live session; the eventual save claims it.
- **Save:** painlog add (unchanged, local, keyed by server-issued sessionId) → best-effort `claimSession` → records.

- [ ] **Step 1: Rewrite `SessionView.vue` script block:**

```typescript
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { useRouter } from 'vue-router'
import AppIcon from '@/components/AppIcon.vue'
import PainScale from '@/components/PainScale.vue'
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
}

function startPolling() {
  stopPolling()
  poller = setInterval(poll, 2000)
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
```

- [ ] **Step 2: Rework the template's before/starting and running blocks** (after phase unchanged):

```html
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
  <p class="clock">{{ clock }}</p>
  <p class="reps">จำนวนครั้ง: {{ reps }}</p>
  <p class="hint">{{ connLost ? 'กำลังเชื่อมต่อ…' : 'เครื่องกำลังทำงาน บริหารเข่าตามจังหวะของอุปกรณ์' }}</p>
  <button class="primary wide" @click="stop"><AppIcon name="stop" /> สั่งเครื่องหยุด</button>
</template>
```

Add to the scoped styles:

```css
.reps {
  font-size: 1.5rem;
  text-align: center;
  font-variant-numeric: tabular-nums;
  margin-bottom: 0.5rem;
}
```

- [ ] **Step 3: Delete the v2 client** — in `device.ts` remove `startSession`, `getStatus`, `stopSession` and the `DeviceStatus` import; in `types.ts` remove the `DeviceStatus` interface; in `device.test.ts` remove the `startSession`/`getStatus`/`stopSession` describes and their imports, and repoint the `request timeout` test at `getCurrent` (expected fetched URL `http://dev/api/sessions/current`).

- [ ] **Step 4: Run tests + build**

Run: `npm run test` — expected: PASS.
Run: `npm run build` — expected: clean (proves nothing else referenced the deleted v2 client or `DeviceStatus`).

- [ ] **Step 5: Commit**

```bash
git add src/views/SessionView.vue src/lib/device.ts src/lib/device.test.ts src/types.ts
git commit -m "feat: SessionView on server sessions with attach flow, reps, and claim"
```

---

### Task 4: Settings — remove the device address field

**Files:**
- Modify: `PT-Leg-Tracker/src/types.ts` (drop `deviceUrl` from `Settings`)
- Modify: `PT-Leg-Tracker/src/stores/settings.ts` (drop from `DEFAULTS`)
- Modify: `PT-Leg-Tracker/src/stores/settings.test.ts` (drop the `deviceUrl` assertion)
- Modify: `PT-Leg-Tracker/src/lib/alarm.test.ts:54` (drop the `deviceUrl: ''` key from the settings fixture)
- Modify: `PT-Leg-Tracker/src/views/SettingsView.vue` (drop the field + ref; retitle card)

**Interfaces:**
- Consumes: after Task 3, no `src/` code reads `settings.deviceUrl` (verify with the grep in Step 1).
- Produces: `Settings` = `{ fontLarge, serverUrl, lastSync, alarmEnabled, alarmTime }`. Old persisted blobs containing `deviceUrl` still hydrate fine (`{ ...DEFAULTS, ...stored }` just carries a harmless extra key at runtime).

- [ ] **Step 1: Verify no remaining consumers**

Run (from `PT-Leg-Tracker/`): `git grep -n deviceUrl -- src`
Expected: only `types.ts`, `stores/settings.ts`, `stores/settings.test.ts`, `lib/alarm.test.ts`, `views/SettingsView.vue` — the files this task deletes it from. Any other hit = a missed consumer; fix that first.

- [ ] **Step 2: Update the failing test first** — in `settings.test.ts` delete the line `expect(DEFAULTS.deviceUrl).toBe('')`. In `alarm.test.ts` delete the `deviceUrl: '',` line from the fixture object.

- [ ] **Step 3: Remove the field**
  - `types.ts`: delete `deviceUrl: string` from `Settings`.
  - `stores/settings.ts`: delete `deviceUrl: '',` from `DEFAULTS`.
  - `SettingsView.vue`: delete `const deviceUrl = ref(settings.settings.deviceUrl)`; change `saveServer` to `await settings.update({ serverUrl: serverUrl.value.trim() })`; delete the `ที่อยู่เครื่องบริหาร` label/input line; change the card heading `เซิร์ฟเวอร์และเครื่องบริหาร (สำหรับเจ้าหน้าที่)` → `เซิร์ฟเวอร์ (สำหรับเจ้าหน้าที่)`.

- [ ] **Step 4: Run tests + build**

Run: `npm run test` — expected: PASS.
Run: `npm run build` — expected: clean (vue-tsc catches any leftover `deviceUrl` reference).

- [ ] **Step 5: Commit**

```bash
git add src/types.ts src/stores/settings.ts src/stores/settings.test.ts src/lib/alarm.test.ts src/views/SettingsView.vue
git commit -m "feat: drop device address setting; server URL carries control traffic"
```

---

### Task 5: Retire mock-device, end-to-end check, final gates

**Files:**
- Delete: `PT-Leg-Tracker/scripts/mock-device.mjs` (replaced by `server/scripts/mock-esp32.mjs`, which already exists)

**Interfaces:**
- Consumes: everything above; `server/` run instructions from `server/README.md`.
- Produces: v3 branch state ready for review/merge.

- [ ] **Step 1: Delete the retired mock**

```bash
git rm PT-Leg-Tracker/scripts/mock-device.mjs
```

Then `git grep -n "mock-device" -- PT-Leg-Tracker server` — expected: no hits (design docs/old plans may still mention it; those are historical records, leave them).

- [ ] **Step 2: End-to-end check (manual, three terminals + browser)**

1. Terminal A — server (from `server/`): `.venv\Scripts\python -m uvicorn app.main:app --host 0.0.0.0 --port 8000` (use a scratch DB if preferred: set `PTLT_DB` to a temp path first).
2. Terminal B — mock device (from `server/`): `node scripts/mock-esp32.mjs http://localhost:8000`
3. Terminal C — app (from `PT-Leg-Tracker/`): `npm run dev`, open the printed URL.
4. In the app: Settings → set server URL `http://localhost:8000` → บันทึก.
5. Session flow: pick pain-before → สั่งเครื่องเริ่มทำงาน → shows กำลังสั่งเครื่อง... then flips to running with clock + จำนวนครั้ง within ~4 s → สั่งเครื่องหยุด → pain-after → บันทึกผล.
6. Records: Settings → ดึงข้อมูลจากเครื่องบริหาร → the session appears in Records (claim + sync round-trip works).
7. Attach: press `b` in the mock-esp32 terminal (device-origin start) → reopen the Session screen → attach offer appears → pick pain-before → เข้าร่วมเซสชันที่กำลังทำงานอยู่ → live clock/reps → stop → save → session claimed.
8. Offline error: quit mock-esp32 (`q`), wait >10 s → start from the app → เครื่องออฟไลน์ appears (fast-fail on `deviceOnline: false`).
9. Busy 409: with mock running and a session started from the device, tap start → เครื่องกำลังทำงานอยู่แล้ว + attach offer.

Record pass/fail per step; any failure → fix before proceeding (systematic-debugging if non-obvious).

- [ ] **Step 3: Final gates** (from `PT-Leg-Tracker/`)

Run: `npm run test` — PASS; `npm run lint` — clean; `npm run build` — clean.
Server regression (from `server/`): `.venv\Scripts\python -m pytest` — PASS.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "chore: retire mock-device script superseded by server mock-esp32"
```

---

## Post-plan

After Task 5, use superpowers:finishing-a-development-branch (branch `feature/app-screens` holds v1+v2+server+v3, still unmerged to `main`). Android smoke test (`npm run build` + `npx cap sync android` + phone on LAN) remains the final device gate before contest use.
