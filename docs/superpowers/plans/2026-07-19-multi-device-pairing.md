# Multi-Device Pairing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Scope live device control per device code so multiple ESP32s and phones run concurrently without crossing, while patient history stays merged per patient.

**Architecture:** The server's single in-memory `DeviceState` becomes a `dict[str, DeviceState]` keyed by device code, TOFU-created on first contact. DB session queries gain a `device_id` filter. Every device/app control endpoint carries an optional `deviceId` (default `"default"`, keeping existing tests green). The app adds a `deviceCode` setting sent as `?deviceId=`. Patient history (`patientCode`), claim (`sessionId`), and records sync are unchanged.

**Tech Stack:** Server: FastAPI + stdlib sqlite3, pytest. App: Vue 3 + TS, Vitest.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-19-multi-device-pairing-design.md`.
- App commands run from `PT-Leg-Tracker/`; server commands from `server/` (venv at `server/.venv`, run as `.venv/Scripts/python.exe -m pytest`).
- `deviceId` is optional on every endpoint; absent → constant `DEFAULT_DEVICE_ID = "default"`. Existing 47 pytest + 44 vitest must stay green throughout.
- Trust-on-first-use: any `deviceId` naming a device auto-creates its state; no allowlist.
- `patientCode` (history), `sessionId` (claim), records sync: UNCHANGED.
- No AI attribution lines in commits. Commit after every task.
- Firmware (LVGL device-code entry, sending the code) is OUT OF SCOPE here — folds into `docs/superpowers/plans/2026-07-19-esp32-firmware.md`. This plan covers server + app + mock.

## File Structure

- `server/app/db.py` — add `DEFAULT_DEVICE_ID`; `device_id` param on `create_session`, `get_open_session`.
- `server/app/service.py` — `dict[str, DeviceState]` + `_device()` TOFU getter + `.device` back-compat property; `device_id` param on every ESP32/app method.
- `server/app/routes_device.py`, `routes_app.py` — `deviceId` query/body param threaded to the service.
- `server/scripts/mock-esp32.mjs` — 2nd CLI arg `deviceCode`, sent on command/event/heartbeat.
- `PT-Leg-Tracker/src/types.ts`, `stores/settings.ts`, `views/SettingsView.vue` — `deviceCode` setting + field.
- `PT-Leg-Tracker/src/lib/device.ts` — `deviceCode` param + `?deviceId=`, new `'no-device'` error kind.
- `PT-Leg-Tracker/src/views/SessionView.vue` — pass `deviceCode`, map `no-device` → Thai.

---

### Task 1: DB — device-scoped sessions

**Files:**
- Modify: `server/app/db.py:10` (constant), `:38-45` (`create_session`), `:56-60` (`get_open_session`)
- Test: `server/tests/test_db.py`

**Interfaces:**
- Produces: `db.DEFAULT_DEVICE_ID = "default"`; `db.create_session(started_at: str, origin: str, device_id: str = DEFAULT_DEVICE_ID) -> str`; `db.get_open_session(device_id: str = DEFAULT_DEVICE_ID) -> sqlite3.Row | None`. Consumed by Task 2.

- [ ] **Step 1: Write the failing test** — append to `server/tests/test_db.py`:

```python
def test_open_sessions_are_isolated_per_device():
    a = db.create_session("2026-07-13T10:00:00+00:00", "app", "KNEE-01")
    b = db.create_session("2026-07-13T10:00:01+00:00", "device", "KNEE-02")
    assert db.get_open_session("KNEE-01")["id"] == a
    assert db.get_open_session("KNEE-02")["id"] == b
    db.close_session(a, "2026-07-13T10:05:00+00:00", 300, 5)
    assert db.get_open_session("KNEE-01") is None
    assert db.get_open_session("KNEE-02")["id"] == b


def test_create_session_defaults_to_default_device():
    sid = db.create_session("2026-07-13T10:00:00+00:00", "app")
    assert db.get_open_session()["id"] == sid
    assert db.get_open_session("KNEE-99") is None
```

- [ ] **Step 2: Run to verify it fails**

Run: `.venv/Scripts/python.exe -m pytest tests/test_db.py -v`
Expected: FAIL — `get_open_session()` takes no argument / returns cross-device row.

- [ ] **Step 3: Implement** — in `server/app/db.py`, replace the `DEVICE_ID` constant:

```python
DEFAULT_DEVICE_ID = "default"
```

Replace `create_session`:

```python
def create_session(
    started_at: str, origin: str, device_id: str = DEFAULT_DEVICE_ID
) -> str:
    session_id = str(uuid.uuid4())
    with _db() as conn:
        conn.execute(
            "INSERT INTO sessions (id, device_id, started_at, origin) VALUES (?, ?, ?, ?)",
            (session_id, device_id, started_at, origin),
        )
    return session_id
```

Replace `get_open_session`:

```python
def get_open_session(device_id: str = DEFAULT_DEVICE_ID) -> sqlite3.Row | None:
    with _db() as conn:
        return conn.execute(
            "SELECT * FROM sessions"
            " WHERE ended_at IS NULL AND device_id = ?"
            " ORDER BY started_at DESC LIMIT 1",
            (device_id,),
        ).fetchone()
```

- [ ] **Step 4: Run to verify it passes**

Run: `.venv/Scripts/python.exe -m pytest tests/test_db.py -v`
Expected: PASS (new + existing db tests).

- [ ] **Step 5: Commit**

```bash
git add server/app/db.py server/tests/test_db.py
git commit -m "feat(server): device-scoped open-session query"
```

---

### Task 2: Service — per-device state dictionary

**Files:**
- Modify: `server/app/service.py` (whole `SessionService` class body)
- Test: `server/tests/test_service.py`

**Interfaces:**
- Consumes: `db.get_open_session(device_id)`, `db.create_session(..., device_id)`, `db.DEFAULT_DEVICE_ID` (Task 1).
- Produces: every ESP32/app method gains a trailing `device_id: str = DEFAULT_DEVICE_ID` param — `fetch_command(device_id)`, `handle_event(event_type, elapsed_sec, reps, device_id)`, `handle_heartbeat(state, elapsed_sec, reps, device_id)`, `queue_start(device_id)`, `queue_stop(device_id)`, `current(device_id)`. `SessionService.device` stays a property → default device's `DeviceState`. Consumed by Task 3.

- [ ] **Step 1: Write the failing test** — append to `server/tests/test_service.py`:

```python
from app.db import DEFAULT_DEVICE_ID
from app.service import SessionService


def test_two_devices_have_isolated_queues_and_sessions(clock):
    svc = SessionService(now=clock)
    svc.queue_start("KNEE-01")
    # only KNEE-01's queue has the command
    assert svc.fetch_command("KNEE-02") is None
    assert svc.fetch_command("KNEE-01") == "start"
    # KNEE-01 starts; KNEE-02 stays idle
    svc.handle_event("started", 0, 0, "KNEE-01")
    assert svc.current("KNEE-01")["state"] == "running"
    assert svc.current("KNEE-02")["state"] == "idle"
    # stopping KNEE-01 does not touch KNEE-02
    svc.handle_event("stopped", 40, 2, "KNEE-01")
    assert svc.current("KNEE-01")["state"] == "idle"
    assert svc.current("KNEE-02")["sessionId"] is None


def test_default_device_backcompat(clock):
    svc = SessionService(now=clock)
    svc.queue_start()  # no device_id
    assert svc.fetch_command() == "start"
    assert svc.device is svc._device(DEFAULT_DEVICE_ID)
```

- [ ] **Step 2: Run to verify it fails**

Run: `.venv/Scripts/python.exe -m pytest tests/test_service.py -v`
Expected: FAIL — methods take no `device_id`; no `_device`.

- [ ] **Step 3: Implement** — in `server/app/service.py`, update imports and rewrite the class. Change the import line `from .state import DeviceState` to also pull the default id:

```python
from . import db
from .db import DEFAULT_DEVICE_ID
from .state import DeviceState
```

Replace `__init__` and add the getter/property (replaces `self.device = DeviceState(now=now)`):

```python
    def __init__(self, now=time.time):
        self._now = now
        self._devices: dict[str, DeviceState] = {}

    def _device(self, device_id: str) -> DeviceState:
        dev = self._devices.get(device_id)
        if dev is None:
            dev = DeviceState(now=self._now)
            self._devices[device_id] = dev
        return dev

    @property
    def device(self) -> DeviceState:
        """Back-compat accessor for the default device."""
        return self._device(DEFAULT_DEVICE_ID)
```

Rewrite each method to take `device_id` and use a local `dev`. Replace the ESP32-facing block:

```python
    def fetch_command(self, device_id: str = DEFAULT_DEVICE_ID) -> str | None:
        return self._device(device_id).fetch_command()

    def handle_event(
        self,
        event_type: str,
        elapsed_sec: int,
        reps: int,
        device_id: str = DEFAULT_DEVICE_ID,
    ) -> str | None:
        dev = self._device(device_id)
        if event_type == "started":
            stale = db.get_open_session(device_id)
            if stale is not None:
                db.close_session(
                    stale["id"], self._iso_now(), dev.live_elapsed_sec, dev.live_reps
                )
            origin = "app" if dev.consume_start_origin() else "device"
            dev.clear_pending_start()
            session_id = db.create_session(self._iso_now(), origin, device_id)
            dev.heartbeat("running", elapsed_sec, reps)
            return session_id

        open_session = db.get_open_session(device_id)
        dev.heartbeat("idle", elapsed_sec, reps)
        dev.clear_start_in_flight()
        if open_session is None:
            return None
        db.close_session(open_session["id"], self._iso_now(), elapsed_sec, reps)
        return open_session["id"]

    def handle_heartbeat(
        self,
        state: str,
        elapsed_sec: int,
        reps: int,
        device_id: str = DEFAULT_DEVICE_ID,
    ) -> None:
        dev = self._device(device_id)
        prev_elapsed = dev.live_elapsed_sec
        prev_reps = dev.live_reps
        dev.heartbeat(state, elapsed_sec, reps)
        if state == "idle":
            open_session = db.get_open_session(device_id)
            if open_session is not None:
                db.close_session(
                    open_session["id"], self._iso_now(), prev_elapsed, prev_reps
                )
```

Replace the app-facing block (`queue_start` through `current`):

```python
    def queue_start(self, device_id: str = DEFAULT_DEVICE_ID) -> None:
        self._auto_close_if_silent(device_id)
        dev = self._device(device_id)
        if db.get_open_session(device_id) is not None:
            raise ConflictError("session already running")
        if dev.pending_command() == "start":
            raise ConflictError("start already queued")
        if dev.start_in_flight():
            raise ConflictError("start in progress")
        dev.queue_command("start")

    def queue_stop(self, device_id: str = DEFAULT_DEVICE_ID) -> None:
        self._auto_close_if_silent(device_id)
        if db.get_open_session(device_id) is None:
            raise ConflictError("no session running")
        self._device(device_id).queue_command("stop")

    def current(self, device_id: str = DEFAULT_DEVICE_ID) -> dict:
        self._auto_close_if_silent(device_id)
        dev = self._device(device_id)
        online = dev.device_online()
        open_session = db.get_open_session(device_id)
        if open_session is not None:
            return {
                "sessionId": open_session["id"],
                "state": "running",
                "elapsedSec": dev.live_elapsed_sec,
                "reps": dev.live_reps,
                "deviceOnline": online,
            }
        state = (
            "starting"
            if dev.pending_command() == "start" or dev.start_in_flight()
            else "idle"
        )
        return {
            "sessionId": None,
            "state": state,
            "elapsedSec": 0,
            "reps": 0,
            "deviceOnline": online,
        }
```

Replace `_auto_close_if_silent` (keep `claim` and `list_for_patient` exactly as they are — no device scoping):

```python
    def _auto_close_if_silent(self, device_id: str = DEFAULT_DEVICE_ID) -> None:
        dev = self._device(device_id)
        silent_for = dev.seconds_since_heartbeat()
        if silent_for is None or silent_for <= AUTO_CLOSE_AFTER_SEC:
            return
        open_session = db.get_open_session(device_id)
        if open_session is not None:
            db.close_session(
                open_session["id"], self._iso_now(), dev.live_elapsed_sec, dev.live_reps
            )
            dev.live_state = "idle"
```

- [ ] **Step 4: Run to verify it passes**

Run: `.venv/Scripts/python.exe -m pytest tests/test_service.py tests/test_device_routes.py -v`
Expected: PASS. (`test_device_routes` uses `service.device.device_online()` — the back-compat property keeps it green.)

- [ ] **Step 5: Commit**

```bash
git add server/app/service.py server/tests/test_service.py
git commit -m "feat(server): per-device session state dictionary"
```

---

### Task 3: Routes — thread deviceId through endpoints

**Files:**
- Modify: `server/app/routes_device.py`, `server/app/routes_app.py`
- Test: `server/tests/test_device_routes.py`, `server/tests/test_app_routes.py`

**Interfaces:**
- Consumes: service methods with `device_id` (Task 2).
- Produces: `GET /api/device/command?deviceId=`, `POST /api/device/event` + `/heartbeat` bodies with `deviceId`, `POST /api/sessions/start?deviceId=`, `/stop?deviceId=`, `GET /api/sessions/current?deviceId=` — all default `"default"`. Consumed by Task 6 (app) and Task 4 (mock).

- [ ] **Step 1: Write the failing test** — append to `server/tests/test_device_routes.py`:

```python
def test_command_endpoint_scoped_by_device_id(clock):
    client = make_client(clock)
    client.app.state.service.queue_start("KNEE-01")
    assert client.get("/api/device/command", params={"deviceId": "KNEE-02"}).json() == {
        "command": None
    }
    assert client.get("/api/device/command", params={"deviceId": "KNEE-01"}).json() == {
        "command": "start"
    }


def test_heartbeat_tofu_creates_device(clock):
    client = make_client(clock)
    client.post(
        "/api/device/heartbeat",
        json={"state": "running", "elapsedSec": 5, "reps": 1, "deviceId": "KNEE-07"},
    )
    assert client.app.state.service._device("KNEE-07").device_online() is True
```

Append to `server/tests/test_app_routes.py`:

```python
def test_sessions_isolated_by_device_id(clock):
    from fastapi.testclient import TestClient
    from app.main import create_app
    from app.service import SessionService

    client = TestClient(create_app(service=SessionService(now=clock)))
    client.post("/api/sessions/start", params={"deviceId": "KNEE-01"})
    # KNEE-02 sees no running/queued state
    assert client.get(
        "/api/sessions/current", params={"deviceId": "KNEE-02"}
    ).json()["state"] == "idle"
    assert client.get(
        "/api/sessions/current", params={"deviceId": "KNEE-01"}
    ).json()["state"] == "starting"
```

- [ ] **Step 2: Run to verify it fails**

Run: `.venv/Scripts/python.exe -m pytest tests/test_device_routes.py tests/test_app_routes.py -v`
Expected: FAIL — endpoints ignore `deviceId`.

- [ ] **Step 3: Implement** — in `server/app/routes_device.py`, add `deviceId` to the bodies and command query. Update the models:

```python
class EventBody(BaseModel):
    type: Literal["started", "stopped"]
    elapsedSec: int = 0
    reps: int = 0
    deviceId: str = "default"


class HeartbeatBody(BaseModel):
    state: Literal["idle", "running"]
    elapsedSec: int = 0
    reps: int = 0
    deviceId: str = "default"
```

Update the three handlers:

```python
@router.get("/command")
def get_command(request: Request, deviceId: str = "default"):
    return {"command": request.app.state.service.fetch_command(deviceId)}


@router.post("/event")
def post_event(body: EventBody, request: Request):
    session_id = request.app.state.service.handle_event(
        body.type, body.elapsedSec, body.reps, body.deviceId
    )
    return {"sessionId": session_id}


@router.post("/heartbeat")
def post_heartbeat(body: HeartbeatBody, request: Request):
    request.app.state.service.handle_heartbeat(
        body.state, body.elapsedSec, body.reps, body.deviceId
    )
    return {"ok": True}
```

In `server/app/routes_app.py`, add `deviceId` to the three control handlers (leave `list_sessions` and `claim` untouched):

```python
@router.post("/start", status_code=202)
def start(request: Request, deviceId: str = "default"):
    try:
        request.app.state.service.queue_start(deviceId)
    except ConflictError as exc:
        raise HTTPException(status_code=409, detail=str(exc))
    return {"queued": True}


@router.post("/stop", status_code=202)
def stop(request: Request, deviceId: str = "default"):
    try:
        request.app.state.service.queue_stop(deviceId)
    except ConflictError as exc:
        raise HTTPException(status_code=409, detail=str(exc))
    return {"queued": True}


@router.get("/current")
def current(request: Request, deviceId: str = "default"):
    return request.app.state.service.current(deviceId)
```

- [ ] **Step 4: Run the full server suite**

Run: `.venv/Scripts/python.exe -m pytest -v`
Expected: PASS — all existing 47 + new tests.

- [ ] **Step 5: Commit**

```bash
git add server/app/routes_device.py server/app/routes_app.py server/tests/test_device_routes.py server/tests/test_app_routes.py
git commit -m "feat(server): deviceId param on device + session endpoints"
```

---

### Task 4: Mock ESP32 — deviceId argument

**Files:**
- Modify: `server/scripts/mock-esp32.mjs`

**Interfaces:**
- Consumes: endpoints from Task 3.
- Produces: `node scripts/mock-esp32.mjs <serverUrl> <deviceCode>` (deviceCode default `"default"`), sending the code on command/event/heartbeat.

- [ ] **Step 1: Add the deviceId constant** — in `server/scripts/mock-esp32.mjs`, under the `base` line:

```js
const base = process.argv[2] || 'http://localhost:8000'
const deviceId = process.argv[3] || 'default'
```

- [ ] **Step 2: Send deviceId in the snapshot** — change `snapshot()` so events/heartbeats include it:

```js
const snapshot = () => ({
  deviceId,
  elapsedSec: running ? Math.floor((Date.now() - startedAt) / 1000) : 0,
  reps: Math.floor(repCounter),
})
```

- [ ] **Step 3: Scope the command poll** — in the 1 s command-poll interval, change the fetch URL:

```js
    const res = await fetch(`${base}/api/device/command?deviceId=${encodeURIComponent(deviceId)}`)
```

- [ ] **Step 4: Update the banner** — change the final `console.log` line:

```js
console.log(`[mock-esp32] device=${deviceId} polling ${base} — "b" = physical button, "q" = quit`)
```

- [ ] **Step 5: Verify manually** — start the server, then two mocks:

```bash
# terminal A (from server/):
.venv/Scripts/python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 8000
# terminal B (from repo root):
node server/scripts/mock-esp32.mjs http://localhost:8000 KNEE-01
# terminal C:
node server/scripts/mock-esp32.mjs http://localhost:8000 KNEE-02
```

In a 4th terminal, queue a start for KNEE-01 only and confirm B (not C) picks it up:

```bash
curl -s -X POST "http://localhost:8000/api/sessions/start?deviceId=KNEE-01"
```

Expected: terminal B prints `started (app command)`, terminal C stays idle.
Stop the mocks (`q`) and server (Ctrl-C).

- [ ] **Step 6: Commit**

```bash
git add server/scripts/mock-esp32.mjs
git commit -m "feat(mock): deviceId arg for multi-device testing"
```

---

### Task 5: App — deviceCode setting + Settings field

**Files:**
- Modify: `PT-Leg-Tracker/src/types.ts`, `src/stores/settings.ts`, `src/views/SettingsView.vue`

**Interfaces:**
- Produces: `Settings.deviceCode: string` (default `''`), persisted like `serverUrl`; a Settings input bound to it. Consumed by Task 6.

- [ ] **Step 1: Add the type field** — in `PT-Leg-Tracker/src/types.ts`, add to the `Settings` interface (find `serverUrl: string` and add below it):

```ts
  deviceCode: string
```

- [ ] **Step 2: Add the default** — in `src/stores/settings.ts` `DEFAULTS`, add under `serverUrl: ''`:

```ts
  deviceCode: '',
```

- [ ] **Step 3: Wire the Settings field** — in `src/views/SettingsView.vue` script, add a ref beside `serverUrl` (after line 21):

```ts
const deviceCode = ref(settings.settings.deviceCode)
```

Change `saveServer` to persist both:

```ts
async function saveServer() {
  await settings.update({
    serverUrl: serverUrl.value.trim(),
    deviceCode: deviceCode.value.trim(),
  })
  syncMessage.value = 'บันทึกแล้ว'
}
```

In the server card template, add the device-code input after the server-url `<label>` (before the บันทึก button, i.e. after line 125):

```html
      <label>รหัสเครื่อง (จากหน้าจอเครื่อง)<input v-model="deviceCode" type="text" placeholder="KNEE-01" /></label>
```

- [ ] **Step 4: Verify build + tests**

Run (from `PT-Leg-Tracker/`): `npm run build && npm test`
Expected: build green, 44 vitest pass.

- [ ] **Step 5: Commit**

```bash
git add PT-Leg-Tracker/src/types.ts PT-Leg-Tracker/src/stores/settings.ts PT-Leg-Tracker/src/views/SettingsView.vue
git commit -m "feat(app): device code setting + Settings field"
```

---

### Task 6: App — device.ts deviceId + no-device error, SessionView wiring

**Files:**
- Modify: `PT-Leg-Tracker/src/lib/device.ts`, `src/views/SessionView.vue`
- Test: `PT-Leg-Tracker/src/lib/device.test.ts`

**Interfaces:**
- Consumes: `Settings.deviceCode` (Task 5); server `?deviceId=` endpoints (Task 3).
- Produces: `queueStart(serverUrl, deviceCode)`, `queueStop(serverUrl, deviceCode)`, `getCurrent(serverUrl, deviceCode)`; `DeviceErrorKind` gains `'no-device'` (thrown when `deviceCode` is blank, before any fetch). `claimSession` unchanged.

- [ ] **Step 1: Write the failing test** — append to `PT-Leg-Tracker/src/lib/device.test.ts`:

```ts
it('appends deviceId to control requests', async () => {
  const calls: string[] = []
  vi.stubGlobal('fetch', (url: string) => {
    calls.push(url)
    return Promise.resolve(new Response(JSON.stringify({ queued: true }), { status: 202 }))
  })
  await queueStart('http://s', 'KNEE-01')
  expect(calls[0]).toContain('/api/sessions/start?deviceId=KNEE-01')
})

it('throws no-device when device code is blank', async () => {
  const fetchSpy = vi.fn()
  vi.stubGlobal('fetch', fetchSpy)
  await expect(queueStart('http://s', '  ')).rejects.toMatchObject({ kind: 'no-device' })
  expect(fetchSpy).not.toHaveBeenCalled()
})
```

Ensure the imports at the top of `device.test.ts` include `queueStart` (add if missing).

- [ ] **Step 2: Run to verify it fails**

Run (from `PT-Leg-Tracker/`): `npx vitest run src/lib/device.test.ts`
Expected: FAIL — `queueStart` takes one arg; no `no-device` kind.

- [ ] **Step 3: Implement** — in `src/lib/device.ts`, extend the kind union:

```ts
export type DeviceErrorKind = 'unreachable' | 'busy' | 'idle' | 'offline' | 'no-device'
```

Add a control helper and rewrite the three control functions (leave `request`, `base`, and `claimSession` as-is):

```ts
function control(
  serverUrl: string,
  deviceCode: string,
  path: string,
  conflictKind: DeviceErrorKind,
  init?: RequestInit,
): Promise<unknown> {
  const code = deviceCode.trim()
  if (!code) throw new DeviceError('no-device')
  const sep = path.includes('?') ? '&' : '?'
  const url = `${base(serverUrl)}${path}${sep}deviceId=${encodeURIComponent(code)}`
  return request(url, conflictKind, init)
}

export async function queueStart(serverUrl: string, deviceCode: string): Promise<void> {
  await control(serverUrl, deviceCode, '/api/sessions/start', 'busy', { method: 'POST' })
}

export async function queueStop(serverUrl: string, deviceCode: string): Promise<void> {
  await control(serverUrl, deviceCode, '/api/sessions/stop', 'idle', { method: 'POST' })
}

export async function getCurrent(serverUrl: string, deviceCode: string): Promise<CurrentSession> {
  const d = (await control(serverUrl, deviceCode, '/api/sessions/current', 'unreachable')) as Record<
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
```

- [ ] **Step 4: Run to verify it passes**

Run: `npx vitest run src/lib/device.test.ts`
Expected: PASS.

- [ ] **Step 5: Wire SessionView** — in `src/views/SessionView.vue` script, add a computed beside `serverUrl` (after line 31):

```ts
const deviceCode = computed(() => settings.settings.deviceCode.trim())
```

Pass it to each call: `getCurrent(serverUrl.value, deviceCode.value)` at lines 58 and 151; `queueStart(serverUrl.value, deviceCode.value)` at line 96; `queueStop(serverUrl.value, deviceCode.value)` at line 121.

In `start()`, add a blank-code guard after the `serverUrl` check (after line 92):

```ts
  if (!deviceCode.value) {
    errorMsg.value = 'ยังไม่ได้ตั้งรหัสเครื่อง (ให้เจ้าหน้าที่ตั้งค่าในเมนูตั้งค่า)'
    return
  }
```

Map the `no-device` kind in the `start()` catch — change the `else` branch (line 105-107) to:

```ts
    } else if (e instanceof DeviceError && e.kind === 'no-device') {
      errorMsg.value = 'ยังไม่ได้ตั้งรหัสเครื่อง (ให้เจ้าหน้าที่ตั้งค่าในเมนูตั้งค่า)'
    } else {
      errorMsg.value = 'เชื่อมต่อไม่สำเร็จ ลองใหม่ภายหลัง'
    }
```

Guard the onMounted probe so a blank code doesn't throw uncaught — wrap the probe body (the `getCurrent` at ~line 151) so it only runs when `deviceCode.value` is set; the existing `try/catch` around the probe already swallows errors, so also add `if (!deviceCode.value) return` before the `getCurrent` probe call at line 149's block.

- [ ] **Step 6: Verify build + full app suite**

Run: `npm run build && npm test`
Expected: build green, 44 vitest + the 2 new device tests pass.

- [ ] **Step 7: Commit**

```bash
git add PT-Leg-Tracker/src/lib/device.ts PT-Leg-Tracker/src/lib/device.test.ts PT-Leg-Tracker/src/views/SessionView.vue
git commit -m "feat(app): send deviceId on session control + no-device guard"
```

---

### Task 7: End-to-end multi-device verification

**Files:** none (verification only)

- [ ] **Step 1: Build + start server + two mocks**

```bash
# from PT-Leg-Tracker/:
npm run build
# from server/: server up
.venv/Scripts/python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 8000
# two mocks (repo root), each its own code:
node server/scripts/mock-esp32.mjs http://localhost:8000 KNEE-01
node server/scripts/mock-esp32.mjs http://localhost:8000 KNEE-02
```

- [ ] **Step 2: Two app instances** — open the app in two browser tabs (`npm run dev`) or two phones. In each Settings, set the same server URL; set device code `KNEE-01` in tab 1, `KNEE-02` in tab 2.

- [ ] **Step 3: Concurrent isolation check** — start a session in tab 1 (KNEE-01). Confirm: tab 1 shows running (ring, reps climbing from its mock); tab 2 stays idle. Start tab 2 (KNEE-02) while tab 1 runs; both run independently. Stop tab 1; tab 2 keeps running. Stop tab 2.

- [ ] **Step 4: Blank-code check** — clear the device code in a tab's Settings, try to start: shows "ยังไม่ได้ตั้งรหัสเครื่อง", no session starts.

- [ ] **Step 5: History merge check** — claim both finished sessions to the same patient code (finish the pain-after flow in each tab with the same patientCode in the profile), sync Records: both sessions appear in that patient's history regardless of device.

- [ ] **Step 6: Final commit if any fixups**

```bash
git add -A
git commit -m "fix(multi-device): e2e verification fixups"
```
