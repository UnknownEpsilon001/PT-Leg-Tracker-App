# Offline Patient Data + SD Session Logging (device side) — Design

Date: 2026-07-20
Status: approved by user (brainstorming session 2026-07-20)
Builds on: `2026-07-19-esp32-firmware-design.md` (two-MCU controller + CYD, UART
link) and its `2026-07-20` limit-switch addendum. Depends on the base firmware
plan `docs/superpowers/plans/2026-07-19-esp32-firmware.md` (Tasks 1–13) being
implemented first — this is an extension on top of that firmware.

## Goal

Make the therapy machine **device-first**: a therapist can register and pick a
patient, run a session, and have it logged with a real timestamp **entirely on
the machine**, with no phone and no internet. Patient records and a session log
live on the CYD's microSD card; a DS3231 real-time clock on the controller
supplies correct dates even after power loss. The phone/server path is unchanged
and remains an independent online record.

## Scope

**In scope (this spec):**
- CYD-local patient store on the SD card (list + add).
- A patient **data menu** on the CYD: list patients, add via keyboard, set the
  active patient for the next session.
- **Session logging to SD** on every session end (patient + timestamp +
  duration + reps + fault flag).
- **DS3231 RTC** on the controller, NTP-synced when online, epoch forwarded to
  the CYD over UART for timestamping.
- A **finish screen** on the CYD showing the just-finished session's patient,
  duration, and reps.

**Out of scope (deferred to a later "sync-up" spec):**
- Uploading `/sessions.csv` to the server or merging device logs with the
  phone's history. Online device sessions are *double-recorded* for now (server
  via the existing controller event path **and** SD); the future sync spec
  dedups against the server's session ids.
- Any change to the Vue app or the FastAPI server. Both are untouched by this
  feature.
- Editing or deleting existing patients / sessions on the CYD (append-only).

## Architecture

The two-MCU split is unchanged; this feature adds one peripheral to each board
and new UART traffic between them.

- **CYD (display)** owns the SD card (physically in its slot), the patient
  store, the session log, and all patient UI. It selects the active patient and
  writes the session row.
- **Controller** owns the DS3231 RTC (it has WiFi for NTP and free GPIO). It
  keeps real time and hands the CYD an epoch to stamp each log row.

Rationale for putting the RTC on the controller, not the CYD: the CYD's free
GPIO is nearly exhausted (TFT, touch, SD, and the UART to the controller already
consume its usable pins), while the controller has spare I2C pins and is the
only board that can NTP-sync. One clock, battery-backed, shared over the link.

### Board peripheral map

| Peripheral | Board | Pins |
|---|---|---|
| microSD (SPI) | CYD | CS 5, SCK 18, MISO 19, MOSI 23 (VSPI) — **verify at bring-up** |
| DS3231 RTC (I2C) | Controller | SDA 21, SCL 25 (both free after relays 22/27, UART 16/17, switches 32/33) |

## Data model (SD card, CYD)

Plain CSV, append-friendly, human-readable, survives a card pulled into a PC.

**`/patients.csv`** — one patient per line:

```
code,name,age
KNEE-P001,สมชาย ใจดี,58
KNEE-P002,Somsri K.,63
```

- Loaded fully into a RAM list on boot (clinic-scale: tens, not thousands).
- "Add patient" appends a line and pushes onto the RAM list.
- `code` is the patient's own code (distinct from the machine's device code);
  used as the session-log foreign key. Free text, therapist-assigned.
- Header line written once on first create.

**`/sessions.csv`** — one finished session per line, append-only:

```
datetime,patientCode,patientName,durationSec,reps,fault
2026-07-20T14:32:05,KNEE-P001,สมชาย ใจดี,340,12,0
2026-07-20T15:01:40,-,,180,6,1
```

- `datetime` — ISO-8601 local from the controller epoch; empty/`?`-prefixed if
  the RTC was never set (provisional).
- `patientCode`/`patientName` — the active patient at start; `-`/empty if none
  selected.
- `fault` — `1` if the session ended in the FAULT phase (limit-switch safety),
  else `0`. Faulted sessions are still logged.
- Names may contain Thai; CSV values with a comma or quote are wrapped per RFC
  4180 (double-quote, doubled internal quotes). Thai commas are not ASCII `,`
  so ordinary Thai names need no quoting, but the writer quotes defensively.

## Time (DS3231 on the controller)

- On every successful WiFi connect, the controller does one SNTP sync and writes
  the result to the DS3231.
- The DS3231 is the source of truth thereafter (battery-backed; correct across
  power cuts once set).
- The controller includes the current epoch in the new `done` UART message (and
  MAY include it in periodic `state` messages so the CYD can show a clock later
  — not required here).
- "RTC never set" = DS3231 reports a year < 2020. In that case the controller
  sends `epoch = 0`; the CYD writes the row with a `?`-prefixed placeholder
  datetime so provisional rows are greppable and later fixable.
- Optional manual time-set (CYD settings → UART) is **out of scope**; NTP-set is
  the only path in this spec. A machine that is online even once gets a correct
  clock for the life of the coin cell.

## UART additions (extends the link protocol)

New controller → display message, sent once when a session ends (on the
controller's `stoppedEdge`, covering both clean stops and FAULT):

```json
{"t":"done","elapsed":<sec>,"reps":<n>,"epoch":<unixSeconds|0>,"fault":<0|1>}
```

- `elapsed` = `finalElapsedSec()`, `reps` = `finalReps()` from the session SM.
- `epoch` from the DS3231 (0 if unset).
- `fault` = 1 if the ended phase was FAULT.

No other link messages change. The existing `state` message still drives the
live timer/reps; `done` is the authoritative end-of-session record the CYD logs.

## CYD UI

Built on the LVGL screens from base-firmware Tasks 7–8. Uses the existing
`font_thai` (full Thai block already subset in Task 8) so patient names render.

- **Main running screen — unchanged.** Timer + reps only while running.
- **Idle main screen** gains one button: **"ข้อมูล"** (data), placed beside the
  gear. Visible any time; opens the data menu.
- **Data menu screen:**
  - A scrollable list of patients from the RAM list (`name` — `code`).
  - Tapping a patient sets it **active** (highlighted); the active patient is
    what the next session logs under. Active selection is RAM-only (not
    persisted across reboot — a fresh boot starts with none selected).
  - **"เพิ่มผู้ป่วย"** (add patient) → a small form: code, name, age via the
    LVGL keyboard → **save** appends to `/patients.csv` and the RAM list.
  - Back returns to the main screen.
- **Finish screen:** shown when a `done` message arrives. Displays patient name
  (or "ไม่ระบุ" if none), duration (mm:ss), reps, and a **"บันทึกแล้ว"** (saved)
  confirmation once the row is written. A single "ตกลง" (OK) button returns to
  the main screen. If the session faulted, the screen also shows a red
  "ตรวจสอบสวิตช์" (check switch) note (the FAULT UI from the switch addendum).

Start is allowed with no active patient; the session logs with `patientCode`
`-`. No modal blocks starting — the machine must never refuse to run because
paperwork is missing.

## Failure handling

- **No SD card / mount fails:** the machine still runs (motion + phone path
  unaffected). The data menu shows "ไม่พบการ์ด SD" (no SD card); patient list is
  empty; `done` sessions are held in a small RAM ring (last ~8) and flushed to
  SD if a card is inserted and remounted. Sessions beyond the ring are lost with
  a logged warning — acceptable, SD is expected present.
- **SD write error mid-log:** retried once; on repeated failure the finish
  screen shows "บันทึกไม่สำเร็จ" (save failed) instead of "บันทึกแล้ว"; the row
  is kept in the RAM ring for a later flush attempt. The session is never lost
  silently without the operator seeing it.
- **Malformed `/patients.csv` line:** skipped on load, not fatal; a warning is
  logged over serial.
- **RTC absent / I2C error on the controller:** treated as "unset" → `epoch 0`
  → provisional timestamps; the machine otherwise runs normally.

## Testing

Firmware, so verification is on-target plus a bench-testable file-format check.

1. **SD bring-up:** card mounts on the CYD; `/patients.csv` and `/sessions.csv`
   are created with headers on first boot; no bus conflict with TFT/touch
   (screen still draws, touch still works).
2. **Add + pick patient:** add two patients on the CYD keyboard (one Thai name,
   one Latin); both appear in the list and in `/patients.csv` read on a PC;
   selecting one highlights it as active.
3. **Offline session log (no WiFi, no phone):** pick a patient, run a full
   session, stop; the finish screen shows the right patient/duration/reps and
   "บันทึกแล้ว"; `/sessions.csv` gains a correct row (real datetime if the RTC
   was previously NTP-set on this unit).
4. **No-patient session:** start without selecting; the row logs `-` and empty
   name; the finish screen shows "ไม่ระบุ".
5. **Fault logging:** trigger a limit-switch FAULT mid-session; a row is logged
   with `fault=1` and the finish screen shows the check-switch note.
6. **RTC:** with the controller online once, power-cycle both boards with WiFi
   off; a new offline session still logs the correct current date (DS3231 held
   time). With the RTC disconnected, rows log with a `?`-prefixed datetime and
   the machine still runs.
7. **SD absent:** boot with no card; machine runs; data menu shows the no-card
   message; insert a card and confirm a subsequent session logs.
8. **Parity:** the phone/server path is unaffected — an online session still
   produces its server record exactly as before (existing pytest suite green),
   independent of the SD row.

## Dependencies / build order

This spec's plan runs **after** the base firmware plan
(`2026-07-19-esp32-firmware.md`, Tasks 1–13, including the 2026-07-20 switch
addendum) — it reuses `session.{h,cpp}` (`finalElapsedSec`/`finalReps`,
`takeStoppedEdge`, the FAULT phase), the `link` protocol, `net` (WiFi up event
for the NTP sync), and the CYD `ui` screens. New modules: `rtc.{h,cpp}`
(controller), `store.{h,cpp}` + `sdlog.{h,cpp}` (CYD), plus `ui` and `.ino`
wiring on both boards and the `done` message on the shared `link`.
