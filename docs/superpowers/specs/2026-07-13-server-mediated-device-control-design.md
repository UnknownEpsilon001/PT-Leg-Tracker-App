# Server-Mediated Device Control (v3) — Design

Date: 2026-07-13
Status: approved by user (brainstorming session 2026-07-13)
Supersedes: the "ESP32 remote control over direct LAN HTTP" portion of
`2026-07-12-app-v2-ui-device-control-design.md`. Everything else in the v2 spec
(visual system, pain scale, alarm, global constraints) remains in force.

## Why the pivot

The v2 design had the app talk HTTP directly to the ESP32 on the same WiFi.
Reality: the ESP32 has no IP address reachable from the app. All communication
goes through the server instead:

```
app  ⟷  server  ⟷  ESP32
```

- The ESP32 initiates every connection (it is never called into).
- Control is **dual and linked**: a session can be started/stopped from the
  app OR from a physical button on the device; both paths produce the same
  server-side session.
- Server runs in the cloud eventually; on the local LAN (staff PC) for now.
  One configurable base URL covers both — the app already has `serverUrl` in
  Settings, and the v2 cleartext/mixed-content fixes already permit plain
  `http://` for the LAN phase. A future `https://` cloud URL needs no app
  change.

## Architecture (Approach A — server owns sessions)

The server is the single source of truth for session state. It keeps a
one-slot command queue and a session state machine. The ESP32 stays dumb:
poll for commands, report transitions, heartbeat while alive. The app never
times sessions (unchanged v2 constraint) — it displays server-reported state.

A rejected alternative (device owns state, server as relay) was discarded:
claiming, history, and live status all force server-side storage anyway, and
unclaimed sessions would die with an ESP32 reboot.

**Single device assumption:** exactly one therapy device. Schema reserves a
`deviceId` column for the future; no endpoint takes a device id in v3.

## API contract

Base path `/api`. All bodies JSON. All server responses JSON.

### ESP32-facing

| Endpoint | Body → Response | Semantics |
|---|---|---|
| `GET /api/device/command` | → `{"command": "start" \| "stop" \| null}` | Consume-on-read: returning a command removes it from the queue. Queue holds at most one pending command. |
| `POST /api/device/event` | `{"type": "started" \| "stopped", "elapsedSec": n, "reps": n}` → `{"sessionId": "..."}` | ESP32 reports **every** transition, regardless of origin (button press or fetched command). `started` creates a session row (server issues the id and returns it); `stopped` closes the open session with final `elapsedSec`/`reps`. This single reporting path is what links dual control. |
| `POST /api/device/heartbeat` | `{"state": "idle" \| "running", "elapsedSec": n, "reps": n}` → `{"ok": true}` | ~1 s cadence while running, ~5 s while idle. Updates live numbers and device last-seen. |

Edge semantics:
- `event started` while a session is already open: server closes the stale
  session (power-glitch recovery) and opens a new one.
- `event stopped` with no open session: acknowledged, no-op (`{"sessionId": null}`).

### App-facing

| Endpoint | Body → Response | Semantics |
|---|---|---|
| `POST /api/sessions/start` | → `202 {"queued": true}` | Queues a `start` command. `409` if a session is running or a start is already queued. |
| `POST /api/sessions/stop` | → `202 {"queued": true}` | Queues a `stop` command. `409` if no session running. |
| `GET /api/sessions/current` | → `{"sessionId", "state": "idle" \| "running" \| "starting", "elapsedSec", "reps", "deviceOnline": bool}` | Live view. `starting` = start command queued, `started` event not yet received. `deviceOnline: false` when last heartbeat > 10 s ago. |
| `POST /api/sessions/{id}/claim` | `{"patientCode": "..."}` → `{"ok": true}` | Tags a session to a patient. Idempotent; re-claim overwrites. `404` unknown session. |
| `GET /api/sessions?patientCode=X` | → `[{id, startedAt, endedAt, durationSec, reps, patientCode}]` | Finished sessions for that patient — the app's records sync source. |

### Timing / failure rules (server-enforced)

- **Command expiry:** a queued command not fetched within 30 s is dropped;
  `current` returns to `idle` and the app surfaces a device-offline error
  instead of a fake `starting` state forever.
- **Session auto-close:** no heartbeat for 60 s while `running` → server
  closes the session with last-known `elapsedSec`/`reps` (power loss on
  device). Session remains claimable.
- Worst-case app-start latency: command queue → ESP32 1 s poll → `started`
  event → app 2 s status poll ≈ **3–4 s** to visible "running". Accepted.

## Data model (server, SQLite)

```
sessions:
  id          TEXT PK  (server-issued, e.g. uuid4)
  device_id   TEXT     (reserved; constant for v3)
  started_at  TEXT     (ISO 8601 UTC)
  ended_at    TEXT     (nullable while running)
  duration_sec INTEGER (final, set on close)
  reps        INTEGER
  patient_code TEXT    (nullable = unclaimed)
  origin      TEXT     ("app" | "device")
```

`origin` = `app` when the `started` event follows a fetched start command,
else `device`. In-memory (no table): pending command + queued-at, last
heartbeat + timestamp, live elapsed/reps.

## App changes (v3)

- **`src/lib/device.ts` → server-session client.** Same typed-error style
  (`DeviceError` kinds, UI maps kind → Thai, never raw `.message`), pointed
  at the existing `serverUrl` setting. New error kind for device-offline.
- **Settings:** the v2 "device address" field is removed (server URL field
  remains, now also carries control traffic).
- **SessionView:** keeps the 2 s poll loop shape and the v2 lifecycle guards;
  displays reps alongside elapsed time. New **attach flow**: on mount, if
  `current` reports `running`, offer to attach to that session instead of
  only offering start (this also resolves the v2 final-review "no reattach"
  gap). Post-pain save then claims that session.
- **Claim:** after post-pain save, `POST /api/sessions/{id}/claim` with the
  profile's patientCode. Pain log store unchanged — still local, keyed by
  sessionId (now server-issued). The records/export merge logic survives
  as-is.
- **Records sync:** switches to `GET /api/sessions?patientCode=`.
- **Mocking:** `scripts/mock-device.mjs` is retired. App testing runs the
  real server locally plus a new `scripts/mock-esp32.mjs` that polls
  commands / sends events + heartbeats exactly like firmware.

## ESP32 firmware contract (separate repo; documented here for reference)

- Poll `GET /api/device/command` every 1 s.
- Send `POST /api/device/heartbeat` every 1 s running / 5 s idle.
- On physical button or fetched command: act, then report the transition
  via `POST /api/device/event`. Count reps locally; report cumulative reps
  per session in heartbeats and events.
- No inbound connections; no static IP requirement.

## Error handling (app-visible, Thai)

| Condition | App behavior |
|---|---|
| Server unreachable | existing v2 pattern: "เชื่อมต่อไม่สำเร็จ ลองใหม่ภายหลัง" |
| `deviceOnline: false` / command expired | distinct message "เครื่องออฟไลน์" — tells staff the device, not the server, is the problem |
| `409` on start | "เครื่องกำลังทำงานอยู่แล้ว" + offer attach |
| Poll failure mid-session | keep last-known values + "กำลังเชื่อมต่อ…" (v2 behavior) |

## Security note

Same accepted risk as v2: plain HTTP on trusted LAN for the test phase
(patient session data over cleartext). The cloud phase must use HTTPS —
the app requires no change for that (standard `https://` URL).

## Testing

- **Server:** pytest — state machine transitions (both origins, dual-control
  interleavings), command expiry, auto-close, claim, all endpoint contracts.
- **App:** vitest on the new server-session client (typed errors, timeout)
  and the unchanged merge logic.
- **End-to-end:** real server + `mock-esp32.mjs` + phone on LAN — replaces
  the deferred v2 smoke test as the final gate.

## Delivery: two plans, server first

1. **Plan 1 — server repo** (new repo `PT-Leg-Tracker-Server`, FastAPI +
   SQLite, from scratch; the previously planned repo no longer exists):
   endpoints above, state machine, pytest, `mock-esp32.mjs` equivalent for
   dev, README with run instructions.
2. **Plan 2 — app v3 rework** on top of `feature/app-screens` (be38996):
   client rewrite, SessionView attach flow + reps, Settings field removal,
   records sync switch, mock script swap.

App plan depends on server endpoints existing; firmware implementation is
out of scope for both repos (contract above is its spec).
