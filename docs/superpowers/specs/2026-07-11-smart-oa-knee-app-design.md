# Smart OA Knee App — Design Spec

**Date:** 2026-07-11
**Project:** PT Leg Tracker (Smart OA Knee) — companion Android app for a leg physical therapy device, part of a funded vocational research project (FY 2569, May–Nov 2026).

## Purpose

Support knee osteoarthritis patients (age 40+, 30-person study sample) in learning about their condition, using the therapy device, exercising regularly, and tracking progress. The app is also a research instrument: it must administer pre/post knowledge tests and a satisfaction survey, and its data must be exportable for statistical analysis.

## Key decisions

| Decision | Choice |
|---|---|
| Connectivity model | Offline-first. All features except video playback and server sync work without internet. |
| Users per device | One patient per phone. No login; simple profile created on first launch. |
| Exercise demo media | YouTube embeds for videos (requires internet); bundled photos for device setup step cards (offline). |
| Local storage | Capacitor Preferences storing JSON, with Pinia stores as the runtime layer. Data volume is small (a few hundred records over 7 months). |
| Server | Separate repo, not yet built. ESP32 uploads session data to it over WiFi; the app pulls sessions and merges them into the local log. The app must function fully when the server is unreachable. |
| Language | Thai only. |

## Architecture

- **Stack:** Vue 3 + TypeScript + Vite, Pinia, Vue Router, Capacitor 8 (Android). App code lives in `PT-Leg-Tracker/`.
- **Data flow:** UI → Pinia stores → persistence module (Capacitor Preferences). Stores hydrate from Preferences on app start and persist on every mutation.
- **Sync module:** a single API service with a configurable server base URL. On launch (and manual refresh), it fetches ESP32 sessions for the patient code and merges them into the local session log, deduplicating by session ID. Network failure is silent and non-blocking; the app retries on next launch.
- **Elderly-friendly UI rules:** minimum 20px body font with a larger-font toggle, large touch targets (≥ 48dp), high contrast, at most 4 primary menu items per screen, icon + Thai label on every button.

## Screens

1. **First-launch setup** — collects name, age, and patient code (issued by staff; later used to match server data). Runs once; editable later in Settings.
2. **Home** — four large buttons: ความรู้ (Knowledge), วิธีใช้เครื่อง (Device guide), ออกกำลังกาย (Exercise), บันทึกของฉัน (My records).
3. **Knowledge** — static Thai articles on knee OA (what it is, causes, stages) rendered from bundled JSON/Markdown content. Large text, simple list navigation.
4. **Self-care manual** — same static-content pattern; daily behavior guidance. Presented as a second section inside the Knowledge screen (one screen, two content lists) so Home stays at four buttons.
5. **Device guide** — photo step cards (bundled images, offline) showing device setup and use, plus embedded YouTube videos for motion demonstration.
6. **Guided session** — walks the patient through a therapy session: start → running timer → done → pain rating (0–10 scale with large face icons) → session saved automatically to the log. This is the primary daily-use flow.
7. **My records** — session history list and a simple progress view: pain trend over time and sessions per week.
8. **Quiz engine** — one engine, three question sets loaded from bundled JSON: pre-test (offered on first use), post-test (triggered by staff or by date), satisfaction survey. Multiple-choice, one question per screen, large buttons. Results stored locally with timestamps.
9. **Settings** — export data, edit profile, server URL field, font size toggle.

## Data model

Stored as JSON under Capacitor Preferences keys:

- `profile` — `{ name, age, patientCode, createdAt }`
- `sessions[]` — `{ id, date, durationSec, painBefore?, painAfter, source: 'manual' | 'device' }`
- `quizResults[]` — `{ quizId: 'pre' | 'post' | 'satisfaction', answers[], score, timestamp }`
- `contentProgress` — map of content IDs to read flags (supports research on content usage)

## Data export

Because data is local and the study needs it, Settings includes an **export** action: serializes profile, sessions, and quiz results to a JSON file and opens the Android share sheet (LINE, email, etc.). Staff can collect exports at hospital visits. CSV export of sessions may be added if the researcher prefers spreadsheets.

## Error handling

- Server unreachable: silently skip sync; show last-sync time in Settings.
- Malformed/missing stored data: fall back to empty defaults; never crash on hydrate.
- YouTube offline: show a "video needs internet" placeholder; step-card photos still work.

## Testing

Add Vitest. Unit-test the logic that fails silently: quiz scoring, session merge/dedupe, export serialization. UI verified manually on an Android device.

## Build phases

1. Remove scaffold; app shell, theme, navigation, first-launch profile setup.
2. Static content: knowledge, self-care manual, device guide (photos + YouTube embeds).
3. Guided session, records list, progress chart.
4. Quiz engine + three question sets.
5. Export + server sync (when the server exists).

## Out of scope (for now)

- Multi-patient support / login
- Server implementation (separate repo)
- Push notifications / reminders (possible later addition)
- iOS

## Success criteria (from research proposal)

- App quality rated ≥ "good" by expert review
- Post-test knowledge > pre-test at p < .05 (requires working quiz + export)
- Patient satisfaction ≥ "high" (requires satisfaction survey)
