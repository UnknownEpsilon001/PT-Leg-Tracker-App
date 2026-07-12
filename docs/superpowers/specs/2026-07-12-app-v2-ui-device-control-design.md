# Smart OA Knee App v2 — UI Refresh, Device Control, Exercise Alarm

Date: 2026-07-12. Status: approved by owner. Supersedes parts of `2026-07-11-smart-oa-knee-app-design.md` (session flow, iconography); everything not mentioned here carries over unchanged.

## Goals

1. **UI refresh** — the app must look hand-designed, not AI-generated: no emoji anywhere, consistent visual system, same elderly-friendly constraints.
2. **Device-controlled sessions** — the ESP32 owns all timing/counting. The app never runs its own session timer or countdown. The app remote-controls the device (start/stop) over direct HTTP on the shared WiFi network and displays device-reported status.
3. **Exercise alarm** — one user-configurable daily reminder notification.

## Non-goals / out of scope

- ESP32 firmware changes and server changes (separate repos). This spec defines the HTTP contract the firmware must implement (below) but not its implementation.
- Multiple alarms, per-weekday scheduling.
- BLE or server-mediated device control.

## 1. UI refresh

### Iconography
- New `src/components/AppIcon.vue`: renders an inline SVG by `name` prop. Paths are Lucide-style 24×24 outline icons, stroke-width 2.25–2.5, `stroke="currentColor"`, `fill="none"`, embedded in the component (no network fetch, no icon font, no new dependency).
- Icon set (16, final names may vary): `book-open`, `activity`, `bar-chart`, `settings`, `bell`, `save`, `pencil`, `text-size`, `refresh`, `share`, `play`, `stop` (square), `check`, `chevron-left`, `clipboard`, `star`.
- **Every emoji in the UI is removed** and replaced by an `AppIcon` (buttons, links, records list, quiz screens, topbar back arrow). The Wong-Baker emoji faces on the pain scale are also removed (next section).
- Constraint update: "every primary button = icon + Thai label" now means an `AppIcon` + Thai label.

### Pain scale
- `PainScale.vue` becomes numbered 0–10 buttons on a green → yellow → red color ramp (0 = green, 5 = amber, 10 = red; interpolated steps). Selected state: filled with ramp color, white number, visible border. Unselected: white background, ramp-colored border + number.
- Touch targets stay ≥ 3rem. Labels under the scale: "ไม่ปวด" (left) and "ปวดมากที่สุด" (right).

### Visual polish (all screens)
- One visual system defined in `main.css` custom properties: spacing scale, radius (single card radius), soft shadow token, teal primary palette + warm neutral background (keep current hues, tighten usage).
- Button hierarchy: primary = filled teal; secondary = white with teal border; both keep ≥ 3rem height. No more ad-hoc `.wide`-only styling differences between screens.
- Topbar: back control uses `chevron-left` AppIcon + "กลับ", title centered weight-700.
- Cards: consistent padding, heading size, and gap; Records entries and Settings sections restyled to match.
- All elderly constraints unchanged: Thai-only text (brand name exception stands), body font ≥ 20px + large-font toggle, touch targets ≥ 48dp (≥ 3rem), high contrast, max 4 primary menu items per screen.

## 2. Device-controlled exercise sessions

### Settings addition
- Staff section gains field **ที่อยู่เครื่องบริหาร** (device address), e.g. `http://192.168.0.50` or `http://smartknee.local`. Persisted in the settings store alongside `serverUrl`. Empty = device control unconfigured.

### Firmware HTTP contract (to implement in ESP32 repo)
All endpoints on the device address, JSON, no auth (trusted home LAN), CORS `Access-Control-Allow-Origin: *` required (WebView origin):

| Endpoint | Response | Notes |
|---|---|---|
| `POST /start` | `200 {"sessionId": "<string>"}` | Begins a session; device does all timing/counting. `409` if already running. |
| `GET /status` | `200 {"state": "idle"\|"running", "elapsedSec": <int>, "sessionId": "<string or null>"}` | `elapsedSec`/`sessionId` present while running. |
| `POST /stop` | `200 {"ok": true}` | Ends current session; device finalizes and later uploads it to the server. `409` if idle. |

### WebView mixed-content requirement
The Capacitor WebView serves the app from `https://localhost`, so `fetch` to plain-`http://` LAN addresses (both the device and the staff server) is blocked as mixed content by default. `capacitor.config.ts` must set `android: { allowMixedContent: true }` (+ `npx cap sync`). This also un-breaks the existing server sync for `http://` server URLs; acceptable risk for a trusted-LAN research device.

### New session flow (`SessionView.vue`)
1. **Pain before** — numbered scale, required before start.
2. **เริ่ม** (play icon) → `POST {device}/start` (timeout 5 s).
   - Unreachable/timeout/error → Thai message "เชื่อมต่อเครื่องบริหารไม่ได้ ตรวจสอบว่าโทรศัพท์กับเครื่องอยู่ WiFi เดียวกัน" and stay on step 1. Never crashes.
3. **Running** — poll `GET /status` every 2 s. Screen shows device-reported elapsed time (display only — the app has NO timer of its own; if polls fail transiently, show last known + "กำลังเชื่อมต่อ…"). Button **หยุด** (stop icon) → `POST /stop`. If the device finishes on its own, a poll returns `idle` and the flow advances the same way.
4. **Pain after** — numbered scale → **บันทึกผล**.
5. Save: pain pair stored keyed by the `sessionId` from step 2 (see data model). Navigate to Records.
- Leaving the screen mid-session does not stop the device (device owns the session); polling stops with the screen (interval cleaned up on unmount).

### Data model
- New store record: `painLog { sessionId: string | null, recordedAt: ISO, painBefore: number, painAfter: number }`, persisted via Capacitor Preferences like other stores.
- Server sync (existing) delivers authoritative sessions from the ESP32. Merge rule: a synced session whose `id` matches a `painLog.sessionId` gets that pain pair attached for display/export; matching is by exact id only.
- If `/start` never succeeded (no sessionId) the user cannot reach steps 3–5; there are no orphan pain logs from the normal flow. A painLog whose session never syncs displays in Records as a standalone "บันทึกอาการ" row (date + pain pair, no duration).
- The old app-generated session records (timer-based) are removed from the write path; existing stored sessions remain readable so current data still displays. Export includes painLogs.

## 3. Exercise alarm

- Dependency: `@capacitor/local-notifications` (+ `npx cap sync`).
- Settings gains card **การแจ้งเตือน**: enable toggle + time picker (`<input type="time">`), default off, persisted in settings store (`alarmEnabled: boolean`, `alarmTime: "HH:mm"`).
- On enable: request notification permission (Android 13+). Denied → toggle stays off + Thai message "ไม่ได้รับอนุญาตให้แจ้งเตือน เปิดได้ในตั้งค่าเครื่อง".
- Schedules ONE repeating daily notification (`schedule.on = { hour, minute }`, fixed id) with title "Smart OA Knee", body "ถึงเวลาบริหารเข่าแล้ว". Changing time or disabling cancels the fixed id and reschedules/none. Re-registered on app launch from persisted settings (survives reboot best-effort; exact-alarm precision not required).
- Web build: plugin calls guarded so the web dev server does not crash (feature simply inert on web).

## Error handling summary

- All device HTTP calls: 5 s timeout, try/catch, Thai user-facing messages, no crash on malformed JSON (treat as unreachable).
- Notification scheduling failures: caught, Thai message, toggle reverts.
- Storage writes: existing store conventions (try/catch, memory-first) unchanged.

## Testing

- Vitest units: painLog store (add/persist/merge-by-id), device API module (start/status/stop happy path + timeout/unreachable/409 → typed errors, mocked fetch), alarm scheduling module (permission denied path, schedule/cancel calls, mocked plugin).
- AppIcon: renders named path, unknown name renders nothing (no crash).
- On-device smoke test at the end: UI pass (no emoji anywhere), device flow against a mock ESP32 (tiny local HTTP server on the laptop implementing the 3-endpoint contract), alarm fires.
