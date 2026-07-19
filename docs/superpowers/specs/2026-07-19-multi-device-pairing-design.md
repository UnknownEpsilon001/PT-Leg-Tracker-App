# Multi-Device Pairing (device code) — Design

Date: 2026-07-19
Status: approved by user (brainstorming session 2026-07-19)
Builds on: `2026-07-13-server-mediated-device-control-design.md` (v3, shipped).
Amends the v3 **single-device assumption** to support N concurrent therapy devices.

## Goal

Let multiple therapy devices and multiple phones run at the same time without
crossing wires. Each ESP32 carries a short **device code** (e.g. `KNEE-01`) set
on its own touch screen; a phone drives one device by entering that code. The
server keeps each device's control state fully isolated.

## Why

v3 assumed exactly one device: a single global command queue and a single live
session, no device identity on any endpoint. Two devices on that design race
for the same queue and share one ambiguous `current` session. This spec adds a
device identity so live control is scoped per device, while patient history
stays scoped per patient across all devices.

## Three keys, three jobs

- **`deviceId`** (the device code) — scopes **live control**: which machine a
  phone drives (command queue, start/stop, current live status). New.
- **`patientCode`** — scopes **history**: a patient's records across ALL
  machines. Unchanged. A patient may use `KNEE-01` today and `KNEE-02`
  tomorrow; records stay merged by patientCode.
- **`sessionId`** — scopes **claim**. Unchanged.

## Architecture

**Per-device keyed state.** The server's single in-memory `DeviceState`
(one-slot command queue + liveness + live session numbers) becomes a
dictionary `{deviceId: DeviceState}`. Each device gets an independent
`DeviceState`; N devices = N isolated entries. No other change to `DeviceState`
internals — expiry, auto-close, dual-control all work per device as before.

**Trust on first use (TOFU).** The first request naming a new `deviceId`
auto-creates its `DeviceState`. No pre-registration, no allowlist. Accepted
risk on the trusted LAN test phase, consistent with the v3 cleartext-HTTP
decision.

**Pairing handshake = display + type.** The ESP32 shows its code on its LVGL
screen. Staff reads it and types it into the phone's Settings. That is the
entire handshake — no PIN, no QR, no server round-trip.

**Backward-compatible default.** `deviceId` is optional on every endpoint;
absent → constant `"default"`. This keeps the existing server tests and the
current single-argument mock working as "one device named `default`". New
callers pass a real code.

Rejected alternatives: a device table + registration flow (TOFU chosen, so
overkill); one server process per device (needless).

## API contract

`deviceId` is added as noted. Device-facing endpoints take it as a query
param or body field; app-facing control endpoints take it as a query param.
Missing `deviceId` resolves to `"default"`.

### ESP32-facing

| Endpoint | Change |
|---|---|
| `GET /api/device/command?deviceId=X` | returns the pending command for device X's queue |
| `POST /api/device/event` | body gains `deviceId`; `started` writes `device_id=X` on the session row |
| `POST /api/device/heartbeat` | body gains `deviceId`; first contact TOFU-creates device X |

### App-facing

| Endpoint | Change |
|---|---|
| `POST /api/sessions/start?deviceId=X` | queues `start` for X; `409` scoped to X (X already running / X already queued) |
| `POST /api/sessions/stop?deviceId=X` | queues `stop` for X; `409` if X not running |
| `GET /api/sessions/current?deviceId=X` | live view of device X only |
| `POST /api/sessions/{id}/claim` | **unchanged** — keyed by sessionId |
| `GET /api/sessions?patientCode=P` | **unchanged** — history across all devices |

Edge semantics from v3 (stale-session recovery, no-op stop, command expiry
30 s, session auto-close 60 s, device-offline 10 s) all apply **per device**.

## Data model (server, SQLite)

`sessions.device_id` — reserved-but-constant in v3 — is now populated with the
real device code (`"default"` when unspecified). Column already exists;
**no migration**. `patient_code` unchanged. In-memory: the global `DeviceState`
becomes `dict[str, DeviceState]` keyed by device code.

## App changes

- **`types.ts`:** `Settings.deviceCode: string` (default `""`).
- **Settings store + SettingsView:** add a `deviceCode` field next to
  `serverUrl`, same load/save pattern. Thai label e.g. "รหัสเครื่อง (จากหน้าจอเครื่อง)".
- **Device/session client (`src/lib/api` / `device.ts`):** send `deviceCode`
  as the `deviceId` query param on `start` / `stop` / `current`. If
  `deviceCode` is blank, raise a typed error (new `DeviceError` kind) mapped to
  Thai "ยังไม่ได้ตั้งรหัสเครื่อง" and block session start — same typed-error style as v3
  (never surface raw `.message`).
- **SessionView:** flow unchanged; passes `deviceCode` through the client. The
  attach flow now attaches to the *selected device's* running session.
- **Records sync:** unchanged — still `GET /api/sessions?patientCode=`.

## Firmware contract (separate concern; folds into the pending ESP32 firmware plan)

The not-yet-started ESP32 firmware plan
(`docs/superpowers/plans/2026-07-19-esp32-firmware.md`) absorbs:
- An LVGL settings screen with a device-code text entry, persisted to **NVS**
  (survives reboot), and the code shown on the main screen for staff to read.
- Every `GET /api/device/command`, `POST /api/device/event`,
  `POST /api/device/heartbeat` carries the device code (query param / body per
  the table above).

This spec defines the contract; the firmware plan implements it.

## Mock

`scripts/mock-esp32.mjs` gains a second CLI arg:
`node scripts/mock-esp32.mjs <serverUrl> <deviceCode>`. `deviceCode` defaults
to `"default"`, so the current one-argument invocation is unchanged. The mock
sends the code on command/event/heartbeat.

## Error handling (app-visible, Thai)

| Condition | App behavior |
|---|---|
| `deviceCode` blank | block start + "ยังไม่ได้ตั้งรหัสเครื่อง" (new kind) |
| device X offline / command expired | existing v3 "เครื่องออฟไลน์", now per selected device |
| `409` on start | existing "เครื่องกำลังทำงานอยู่แล้ว" + attach, scoped to X |
| server unreachable | existing v3 pattern |

## Security note

Unchanged from v3: plain HTTP on trusted LAN for the test phase; TOFU adds no
new secret. Cloud phase uses HTTPS with no app change. Device codes are
identifiers, not credentials — anyone on the LAN who knows a code can drive
that device, the same trust model as v3.

## Testing

- **Server pytest:** existing 47 stay green (they omit `deviceId` → `"default"`).
  Add multi-device cases: two devices hold independent queues; concurrent
  sessions on X and Y never cross; TOFU auto-creates on first heartbeat;
  per-device `409`, command expiry, and auto-close.
- **App vitest:** `deviceCode` wiring on the client; blank-code typed error.
- **End-to-end gate:** two `mock-esp32` instances (`KNEE-01`, `KNEE-02`) + two
  browser tabs / phones; concurrent start/stop on each device stays isolated
  (a command to `KNEE-01` never affects `KNEE-02`).

## Delivery

Single implementation plan (server + app; firmware handled by its own plan):
`deviceId` plumbing through server state + endpoints → app Settings field +
client wiring → mock arg → multi-device tests + e2e gate.
