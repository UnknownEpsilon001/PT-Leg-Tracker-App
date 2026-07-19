# App v4 — Logo, Rebrand + Polish, Animations — Design

Date: 2026-07-19
Status: approved by user (brainstorming session 2026-07-19)
Builds on: `2026-07-13-server-mediated-device-control-design.md` (v3, shipped).
All v3 behavior, routes, stores, and server contracts are unchanged.

## Goal

Brand the app with the college logo (`assets/Logo-removebg.png` — วิทยาลัยการอาชีพฝาง /
เครื่องช่วยบริหารกล้ามเนื้อต้นขา badge), restyle to the logo's palette, and add
animation polish for the contest demo. No structural or behavioral changes.

## 1. Palette + typography

CSS custom properties in `src/assets/main.css`; every screen inherits.

| Token | Value | Replaces / role |
|---|---|---|
| `--color-primary` | `#24395e` navy (sampled from logo) | current teal; headers, primary buttons, links |
| `--color-accent` | `#8c2f39` maroon (from logo) | stop button, highlights, chart line accents |
| `--color-muted` | `#c6ccd6` silver-gray (from logo) | borders, soft fills, disabled |
| `--color-bg` | keep current warm cream | page background |
| `--color-text` | dark navy derived from primary | body text |

Exact final hex values may be tuned by eye against the logo during
implementation; the three roles (navy primary, maroon accent, silver muted)
are fixed.

Font: **Kanit** bundled offline via `@fontsource/kanit`, weights 400/600/700.
Headings 600–700, body 400. Current font *sizes* and touch-target sizes are
unchanged (elderly readability is a hard constraint).

## 2. Logo integration

- **Home screen:** logo image top-center above the greeting. Entrance
  animation: fade + small rise on mount.
- **Launcher icon + splash screen:** generated for all Android densities with
  `@capacitor/assets` from a square-padded 1024×1024 PNG produced from the
  source logo (trim transparent margins, pad to square, solid cream background
  for the icon). Splash = logo centered on cream, light and dark variants same
  art.
- **File locations:** web copy in `PT-Leg-Tracker/src/assets/logo.png`
  (trimmed); icon/splash sources in `PT-Leg-Tracker/assets/` (the
  `@capacitor/assets` convention: `icon-only.png`, `splash.png`,
  `splash-dark.png`). Root `assets/` keeps the originals.
- Generated Android resources are committed (same convention as the rest of
  `android/`).

## 3. Animations

All CSS transitions/keyframes or requestAnimationFrame — **no animation
libraries**. Every animation is wrapped in `@media (prefers-reduced-motion:
no-preference)` so reduced-motion users get a static UI.

**Global**
- Router page transitions: fast fade + slight slide (~150–200 ms) via Vue
  `<Transition>` on the router view.
- Card press feedback: scale-down on `:active` for tappable cards/buttons.
- Menu/list cards: staggered fade-in on mount (CSS `animation-delay` by index).

**Session screen**
- Running: pulsing ring around the timer (CSS keyframes, ~2 s cycle).
- Rep count: pop/scale animation when the number changes (keyed `<Transition>`).
- Idle: gentle "breathing" scale on the start button.

**Fun**
- Confetti burst when the session reaches the after-phase (finish): small
  hand-rolled canvas particle effect (~60 lines, self-contained component,
  auto-removes after ~2 s).
- Records chart: line draws itself in via `stroke-dasharray`/`stroke-dashoffset`
  transition on the existing SVG polyline in `TrendChart.vue`.

## 4. Unchanged (hard constraints)

- Screen structure, routes, Pinia stores, server client, server contracts.
- All Thai copy.
- Font sizes, touch-target sizes, large-text setting behavior.
- 44 vitest tests must stay green; `npm run build` + lint clean.

## Testing

- Existing vitest suite green (no logic changes expected; records/session
  logic untouched).
- Visual verification on the real phone via wireless adb (build → cap sync →
  gradlew → install): every screen, session flow with mock ESP32, launcher
  icon, splash.
- Reduced-motion spot check (Android "remove animations" accessibility
  toggle).

## Delivery

Single implementation plan (app-only; no server work): tokens + font →
logo/home → icon/splash generation → global animations → session animations →
confetti + chart draw → device verification.
